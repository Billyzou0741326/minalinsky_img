#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <curl/curl.h>
#include <json-c/json.h>
#include "net.h"

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#endif  // __linux__


const char *const api = "https://gateway.reddit.com/desktopapi/v1/subreddits/LegendaryMinalinsky?rtj=only&redditWebClient=web2x&app=web2x-client-production&allow_over18=1&include=prefsSubreddit&dist=7&layout=card&sort=hot&geo_filter=US";
const char *const user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:76.0) Gecko/20100101 Firefox/76.0";
const char *const dirname = "images/";
const int CONCURRENT_DOWNLOADS = 50;

struct urls {
  CURLU *url;
  struct urls *next;
};

struct file_writer {
  int fd;
  int closed;
  char *filename;
};

int init_directory(const char *dirname);
struct urls * reddit_get_urls();
int reddit_get_file_open_fd(char *filename);
int reddit_get_file_filter_filename(CURLU *this_url, char **filename, size_t *len);
int reddit_get_file_prepare(CURL *easy_curl, CURLU *this_url, char *filename, int file_fd);
void reddit_get_file_on_done(CURL *easy_curl, CURLcode status_code, void *on_done_data);
struct urls * reddit_get_files(struct net_global *n_global, struct urls *urls, int *fill);

size_t curl_write_file_cb(char *data, size_t size, size_t nmemb, struct file_writer *f_writer);
size_t curl_write_json_cb(char *data, size_t size, size_t nmemb, struct json_parser *j_parser);
size_t curl_write_text_cb(char *data, size_t size, size_t nmemb, struct string *text);
size_t curl_write_binary_cb(char *data, size_t size, size_t nmemb, struct string *raw);

int on_timer(struct net_global *n_global, int events);
int on_event(struct net_global *n_global, int fd, int events);

int
main(int argc, char *argv[]) {
  int status = 0;
  int epollfd = 0, timerfd = 0, quitfd;
  const int MAX_EPOLL = 65536;
  struct epoll_event ep_ev, ep_events[MAX_EPOLL];
  struct itimerspec timer;
  struct net_global n_global;
  int mask = 0022;
  CURLMcode code;

  (void) ep_events;

  if (!CURL_AT_LEAST_VERSION(7, 62, 0)) {
    fprintf(stderr, "curl required minimum version: 7.62.0\n");
    exit(EXIT_FAILURE);
  }

  init_directory(dirname);

  umask(mask);

  curl_global_init(CURL_GLOBAL_ALL);
  net_global_init(&n_global);
  n_global.running = 0;

  /** create epoll */
  memset(&ep_ev, 0, sizeof(ep_ev));
  epollfd = epoll_create(1);
  assert(-1 != epollfd);
  n_global.epollfd = epollfd;

  /** create quitfd */
  quitfd = eventfd(0, EFD_NONBLOCK);
  assert(-1 != quitfd);
  printf("quitfd(%d)\n", quitfd);

  /** register quitfd */
  ep_ev.events = EPOLLIN | EPOLLET;
  ep_ev.data.fd = quitfd;
  status = epoll_ctl(epollfd, EPOLL_CTL_ADD, quitfd, &ep_ev);
  assert(-1 != status);

  /** create timer */
  timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  assert(-1 != timerfd);
  n_global.timerfd = timerfd;
  printf("timerfd(%d)\n", timerfd);

  /** set timer */
  memset(&timer, 0, sizeof(timer));
  timer.it_interval.tv_sec = 1;
  timer.it_value.tv_sec = 1;
  status = timerfd_settime(timerfd, 0, &timer, NULL);
  assert(-1 != status);

  /** register timer */
  ep_ev.events = EPOLLIN;
  ep_ev.data.fd = timerfd;
  status = epoll_ctl(epollfd, EPOLL_CTL_ADD, timerfd, &ep_ev);
  assert(0 == status);

  /** curl_multi */
  n_global.multi_curl = curl_multi_init();
  code = curl_multi_setopt(n_global.multi_curl, CURLMOPT_SOCKETFUNCTION, curl_socket_cb);
  code = curl_multi_setopt(n_global.multi_curl, CURLMOPT_SOCKETDATA, &n_global);
  code = curl_multi_setopt(n_global.multi_curl, CURLMOPT_TIMERFUNCTION, curl_timer_cb);
  code = curl_multi_setopt(n_global.multi_curl, CURLMOPT_TIMERDATA, &n_global);
  assert(code == CURLM_OK);

  /** get urls */
  struct urls *urls = NULL;
  urls = reddit_get_urls();
  int fill = 0;

  /** start epoll */
  //**
  for (int round = 0, quit = 0; quit == 0; ++round) {
    fill = CONCURRENT_DOWNLOADS - n_global.running;
    urls = reddit_get_files(&n_global, urls, &fill);

    int nready = status = epoll_wait(epollfd, ep_events, MAX_EPOLL, -1);
    assert(-1 != status);

    if (nready == 0) {
      printf("epollfd(%d) main timeout (round %d)\n", epollfd, round);
    }

    for (int i = 0; i < nready; ++i) {
      int this_fd = ep_events[i].data.fd;
      int this_ev = ep_events[i].events;
      if (this_fd == timerfd) {
        // curl's timer //
        on_timer(&n_global, this_ev);
      } else {
        // curl's socket //
        on_event(&n_global, this_fd, this_ev);
      }
    }

    net_check_multi_info(&n_global);
    if (n_global.running <= 0) {
      if (!urls) {
        quit = 1;
        printf("All done\n");
        memset(&timer, 0, sizeof(timer));
        timerfd_settime(n_global.timerfd, 0, &timer, NULL);
      }
    }
  }
  // */

  curl_multi_cleanup(n_global.multi_curl);
  curl_global_cleanup();

  close(timerfd);
  close(quitfd);
  close(epollfd);

  exit(EXIT_SUCCESS);
}

int
on_timer(struct net_global *n_global, int events) {
  uint64_t count = 0;
  ssize_t err = 0;
  CURLMcode mcode;

  err = read(n_global->timerfd, &count, sizeof(count));
  if (-1 == err) {
    if (errno == EAGAIN) {
      return 0;
    }
  }
  if (sizeof(count) != err) {
    fprintf(stderr, "error read(timerfd): %ld\n", err);
  }

  // printf("curl timer timeout\n");
  mcode = curl_multi_socket_action(n_global->multi_curl, CURL_SOCKET_TIMEOUT, 0, &(n_global->running));
  assert(mcode == CURLM_OK);

  return 0;
}

int
on_event(struct net_global *n_global, int fd, int events) {
  int actions = ((events & EPOLLIN) ? CURL_CSELECT_IN : 0) |
    ((events & EPOLLOUT) ? CURL_CSELECT_OUT : 0);

  curl_multi_socket_action(n_global->multi_curl, fd, actions, &(n_global->running));
  return 0;
}

int
reddit_get_file_open_fd(char *filename) {
  char *file_p_filename = filename;
  int file_p_fd = open(filename, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0644);
  long err = errno;

  if (file_p_fd > 0) {
    printf("opened fd (%d) for (%s)\n", file_p_fd, file_p_filename);
  } else if (err == EEXIST) {
    return -1;
  } else {
    fprintf(stderr, "(%ld) %s (%s)\n", err, strerror(err), filename);
    return -1;
  }

  return file_p_fd;
}

int
reddit_get_file_filter_filename(CURLU *this_url, char **filename, size_t *len) {
  char *path_name = NULL;
  CURLUcode url_code;
  url_code = curl_url_get(this_url, CURLUPART_PATH, &path_name, 0L);
  if (url_code != 0) {
    return 0;
  }

  char *this_filename = NULL;
  size_t filename_len = 0;
  filename_len = snprintf(NULL, 0, "%s%s", dirname, &path_name[1]);
  this_filename = malloc(filename_len+1);
  size_t written = snprintf(this_filename, filename_len+1, "%s%s", dirname, &path_name[1]);
  if (written != filename_len) {
    fprintf(stderr, "(%ld/%ld) %s\n", written, filename_len, this_filename);
    free(this_filename);
    return 0;
  }

  curl_free(path_name);

  if (!filename || !len) {
    free(this_filename);
    return 1;
  }

  *filename = this_filename;
  *len = filename_len;
  return 1;
}

struct file_writer *
file_writer_create() {
  struct file_writer *file_writer = malloc(sizeof(*file_writer));
  if (file_writer) {
    file_writer->fd = 0;
    file_writer->filename = NULL;
    file_writer->closed = 0;
  }
  return file_writer;
}

void
file_writer_destroy(void *f) {
  if (!f)
    return;

  struct file_writer *file_writer = f;

  if (!file_writer->closed) {
    close(file_writer->fd);
    file_writer->closed = 1;
    file_writer->fd = 0;
  }
  free(file_writer);
}

int
reddit_get_file_prepare(CURL *easy_curl, CURLU *this_url, char *filename, int file_fd) {
  CURLcode easy_code;
  struct net_conn *n_conn = net_conn_create();
  struct file_writer *file_writer = NULL;

  file_writer = file_writer_create();
  file_writer->filename = filename;
  file_writer->fd = file_fd;
  file_writer->closed = 0;

  n_conn->easy_curl = easy_curl;
  n_conn->url = this_url;
  n_conn->n_global = NULL;
  n_conn->on_done_data = file_writer;
  n_conn->on_done_data_free = file_writer_destroy;
  n_conn->on_done_callback = reddit_get_file_on_done;

  easy_code = curl_easy_setopt(easy_curl, CURLOPT_CURLU, this_url);
  easy_code = curl_easy_setopt(easy_curl, CURLOPT_NOPROGRESS, 1L);
  easy_code = curl_easy_setopt(easy_curl, CURLOPT_USERAGENT, user_agent);
  easy_code = curl_easy_setopt(easy_curl, CURLOPT_WRITEDATA, file_writer);
  easy_code = curl_easy_setopt(easy_curl, CURLOPT_WRITEFUNCTION, curl_write_file_cb);
  easy_code = curl_easy_setopt(easy_curl, CURLOPT_FOLLOWLOCATION, 1L);
  easy_code = curl_easy_setopt(easy_curl, CURLOPT_BUFFERSIZE, 16 * 1024);
  easy_code = curl_easy_setopt(easy_curl, CURLOPT_PRIVATE, n_conn);
  if (easy_code != CURLE_OK) {
    return -1;
  }

  return 0;
}

void
reddit_get_file_on_done(CURL *easy_curl, CURLcode err, void *on_done_data) {
  if (!on_done_data)
    return;

  struct file_writer *f = on_done_data;
  long status_code = 0;

  curl_easy_getinfo(easy_curl, CURLINFO_RESPONSE_CODE, &status_code);

  printf("(%d) done (%s)\n", f->fd, f->filename);
  if (err != CURLE_OK || status_code != 200) {
    remove(f->filename);
    fprintf(stderr, "err (%s); http status code (%ld)\n", curl_easy_strerror(err), status_code);
  }
  free(f->filename);
}

struct urls *
reddit_get_files(struct net_global *n_global, struct urls *urls, int* fill) {
  if (!n_global || !n_global->multi_curl)
    return urls;
  if (!urls)
    return urls;
  if (!fill || fill <= 0)
    return urls;

  CURLM *multi_curl = n_global->multi_curl;
  int total = 0;
  int max = *fill;

  for (struct urls *current = urls, *previous; current && total < max;) {
    previous = current;

    CURLU *this_url = current->url;

    int file_ok = 0;
    char *this_filename = NULL;
    size_t filename_len = 0;

    file_ok = reddit_get_file_filter_filename(current->url, &this_filename, &filename_len);
    if (!file_ok)
      continue;

    char *file_p_filename = this_filename;
    int file_p_fd = 0;

    file_p_fd = reddit_get_file_open_fd(this_filename);

    if (file_p_fd < 0) {
      if (errno == EEXIST) {
        curl_url_cleanup(current->url);
        this_url = NULL;
        current->url = NULL;
        free(this_filename);

        current = current->next;
        urls = current;
        previous->url = NULL;
        free(previous);
        continue;
      } else {
        fprintf(stderr, "(%d) %s (%s)\n", errno, strerror(errno), this_filename);
        exit(EXIT_FAILURE);
      }
    }

    CURL *this_easy = curl_easy_init();
    int prepare_status = 0;
    prepare_status = reddit_get_file_prepare(this_easy, this_url, file_p_filename, file_p_fd);
    if (prepare_status != 0) {
      continue;
    }

    CURLMcode multi_code;
    multi_code = curl_multi_add_handle(multi_curl, this_easy);
    if (multi_code != 0) {
      printf("CURLMcode(%d)\n", multi_code);
    }

    current = current->next;
    urls = current;
    previous->url = NULL;

    free(previous);
    ++total;
  }
  *fill = total;
  return urls;
}

struct urls *
reddit_get_urls() {
  CURL *easy_curl = curl_easy_init();
  CURLU *url_info = NULL;
  struct urls *urls = NULL;
  struct json_parser *j_parser = NULL;

  j_parser = json_parser_create();

  char after_postid[1024] = {0};

  for (int finished = 0; !finished;) {
    /** Get Postids from API */

    size_t querystr_len = snprintf(NULL, 0, "after=%s", after_postid);
    char *querystr = malloc(querystr_len + 1);
    snprintf(querystr, querystr_len + 1, "after=%s", after_postid);
    printf("querystring: %s\n", querystr);

    CURLUcode url_code;
    url_info = curl_url();
    url_code = curl_url_set(url_info, CURLUPART_URL, api, 0);
    url_code = curl_url_set(url_info, CURLUPART_QUERY, querystr, CURLU_APPENDQUERY | CURLU_URLENCODE);

    if (url_code != 0) {
      printf("CURLUcode(%d)\n", url_code);
    }
    free(querystr);

    CURLcode easy_code;
    easy_code = curl_easy_setopt(easy_curl, CURLOPT_CURLU, url_info);
    easy_code = curl_easy_setopt(easy_curl, CURLOPT_NOPROGRESS, 1L);
    easy_code = curl_easy_setopt(easy_curl, CURLOPT_USERAGENT, user_agent);
    easy_code = curl_easy_setopt(easy_curl, CURLOPT_WRITEDATA, j_parser);
    easy_code = curl_easy_setopt(easy_curl, CURLOPT_WRITEFUNCTION, curl_write_json_cb);
    easy_code = curl_easy_setopt(easy_curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (easy_code != 0) {
      printf("CURLcode(%d)\n", easy_code);
    }

    curl_easy_perform(easy_curl);
    curl_url_cleanup(url_info);

    long status_code = 0;
    curl_easy_getinfo(easy_curl, CURLINFO_RESPONSE_CODE, &status_code);
    printf("http status (%ld)\n", status_code);

    /** Parse Postids from json */
    // printf("%s\n", json_object_to_json_string_ext(j_parser->json, JSON_C_TO_STRING_PRETTY));

    struct array_list *postIdList = NULL;
    json_object *posts = NULL, *data = NULL, *json = NULL;
    json = json_parser_get_json(j_parser);

    json_tokener_reset(j_parser->tok);
    json_object_put(j_parser->json);
    j_parser->json = NULL;

    json_object_object_get_ex(json, "posts", &posts);

    json_object_object_get_ex(json, "postIds", &data);
    postIdList = json_object_get_array(data);               // .postIds: ["", "", ""]
    if (postIdList) {
      printf("Success\n");
    }
    if (!postIdList || array_list_length(postIdList) == 0) {
      /** Cleanup, done */
      json_object_put(json);
      j_parser->json = NULL;
      printf("No postids\n");
      break;
    }

    size_t max = array_list_length(postIdList);
    for (size_t i = 0; i < max; ++i) {
      size_t index = max - i - 1;
      const char *postid = json_object_get_string(array_list_get_idx(postIdList, index));

      if (0 == i && postid) {
        if (!strcmp(after_postid, postid)) {
          finished = 1;
          break;
        }
        snprintf(after_postid, sizeof(after_postid)/sizeof(after_postid[0]), "%s", postid);
        printf("Last postid: (%s/%s)\n", after_postid, postid);
      }

      json_object *content, *type, *external_1, *external_2, *external_3;
      json_object_object_get_ex(posts, postid, &data);                /** .posts.<postid> */
      json_object_object_get_ex(data, "callToAction", &external_1);   /** .posts.<postid>.callToAction */
      json_object_object_get_ex(data, "domainOverride", &external_2); /** .posts.<postid>.domainOverride */
      json_object_object_get_ex(data, "isSponsored", &external_3);    /** .posts.<postid>.isSponsored */
      json_object_object_get_ex(data, "media", &data);                /** .posts.<postid>.media */
      json_object_object_get_ex(data, "content", &content);           /** .posts.<postid>.media.content */
      json_object_object_get_ex(data, "type", &type);                 /** .posts.<postid>.media.type */
      const char *file_type = json_object_get_string(type);
      const char *url = json_object_get_string(content);
      int is_sponsored = json_object_get_boolean(external_3);
      if (!external_1 && !external_2 && !is_sponsored && file_type && !strcmp(file_type, "image") && url) {
        CURLUcode url_code;
        CURLU *url_info = curl_url();
        url_code = curl_url_set(url_info, CURLUPART_URL, url, 0L);
        if (0 == url_code) {
          struct urls *url_node = malloc(sizeof(*url_node));
          url_node->url = url_info;
          url_node->next = urls;
          urls = url_node;
          printf("%s  =>  %s\n", postid, url);
        } else {
          curl_url_cleanup(url_info);
        }
      }
    }

    json_object_put(json);
    j_parser->json = NULL;
  }
  json_parser_destroy(j_parser);
  curl_easy_cleanup(easy_curl);
  return urls;
}

int
init_directory(const char *dirname) {
  /** Create directory to store images */
  int status = mkdir(dirname, 0700);
  if (0 != status && errno != EEXIST) {
    return -1;
  }
  return 0;
}


size_t
curl_write_file_cb(char *data, size_t size, size_t nmemb, struct file_writer *f) {
  assert(f && "NullPointerException");
  if (!f)
    return 0;

  int fd = 0;
  size_t real_size = size * nmemb;
  size_t written = 0;
  struct file_writer *file_writer = f;

  fd = file_writer->fd;

  assert(fd > 0 && "Invalid fd");
  if (fd <= 0)
    return 0;

  written = write(fd, data, real_size);

  return written;
}

size_t
curl_write_text_cb(char *data, size_t size, size_t nmemb, struct string *text) {
  size_t real_size = size * nmemb;

  size_t offset = text->len;

  char *new_str = realloc(text->cstr, text->len + real_size + 1);
  text->cstr = new_str;
  text->len = text->len + real_size;
  if (NULL == new_str)
    return 0;

  memmove(&text->cstr[offset], data, real_size);
  text->cstr[text->len] = '\0';

  return real_size;
}

size_t
curl_write_json_cb(char *data, size_t size, size_t nmemb, struct json_parser *j_parser) {
  if (NULL == j_parser) {
    return 0;
  }
  if (NULL == j_parser->tok) {
    return 0;
  }
  size_t real_size = size * nmemb;
  // printf("%.*s", real_size, data);
  j_parser->json = json_tokener_parse_ex(j_parser->tok, data, real_size);
  j_parser->jerr = json_tokener_get_error(j_parser->tok);
  switch (j_parser->jerr) {
    case json_tokener_success:
      // fall through
    case json_tokener_continue:
      break;
    default:
      return 0;
  }
  return real_size;
}

size_t
curl_write_binary_cb(char *data, size_t size, size_t nmemb, struct string *text) {
  size_t real_size = size * nmemb;

  size_t offset = text->len;

  char *new_str = realloc(text->cstr, text->len + real_size);
  text->cstr = new_str;
  text->len = text->len + real_size;
  if (NULL == new_str)
    return 0;

  memmove(&text->cstr[offset], data, real_size);

  return real_size;
}

