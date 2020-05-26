#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <json-c/json.h>
#include <curl/curl.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

const char *const api = "https://gateway.reddit.com/desktopapi/v1/subreddits/LegendaryMinalinsky?rtj=only&redditWebClient=web2x&app=web2x-client-production&allow_over18=&include=prefsSubreddit&dist=7&layout=card&sort=hot&geo_filter=US";
const char *const user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:76.0) Gecko/20100101 Firefox/76.0";

struct json_parser {
    json_tokener *tok;
    json_object *json;
};

struct text_parser {
    char *cstr;
    size_t len;
};

struct file_parser {
    int fd;
    int closed;
    char *filename;
};

size_t api_write_file_callback(char *data, size_t size, size_t nmemb, void *user_p);
size_t api_write_json_callback(char *data, size_t size, size_t nmemb, void *user_p);
size_t api_write_text_callback(char *data, size_t size, size_t nmemb, void *user_p);
int init_directory(const char *dirname);

int
main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *easy_curl = curl_easy_init();
    CURLU *url_info = NULL;

    const char *dirname = "images/";

    /** Create directory to store images */
    if (0 != init_directory(dirname)) {
        fprintf(stderr, "(%d) %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct urls {
        CURLU *url;
        struct urls *next;
    } *urls = NULL;

    struct json_parser json_data = {0};

    char after_postid[1024] = {0};

    for (int finished = 0; !finished;) {
        /** Get Postids from API */
        json_data.json = NULL;
        json_data.tok = NULL;

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
        easy_code = curl_easy_setopt(easy_curl, CURLOPT_WRITEDATA, &json_data);
        easy_code = curl_easy_setopt(easy_curl, CURLOPT_WRITEFUNCTION, api_write_json_callback);
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
        // printf("%s\n", json_object_to_json_string(json_data.json));

        struct array_list *postIdList = NULL;
        json_object *posts = NULL, *data = NULL, *json = NULL;
        json = json_data.json;

        json_object_object_get_ex(json, "posts", &posts);

        json_object_object_get_ex(json, "postIds", &data);
        postIdList = json_object_get_array(data);               // .postIds: ["", "", ""]
        if (postIdList) {
            printf("Success\n");
        }
        if (!postIdList || array_list_length(postIdList) == 0) {
            /** Cleanup, done */
            json_object_put(json_data.json);
            json_data.json = NULL;
            printf("No postids\n");
            break;
        }

        size_t max = array_list_length(postIdList);
        for (size_t i = 0; i < max; ++i) {
            size_t index = max - i - 1;
            const char *postid = json_object_get_string(array_list_get_idx(postIdList, index));
            printf("%s\n", postid);

            if (0 == i && postid) {
                if (!strcmp(after_postid, postid)) {
                    finished = 1;
                    break;
                }
                snprintf(after_postid, sizeof(after_postid)/sizeof(after_postid[0]), "%s", postid);
                printf("Last postid: (%s/%s)\n", after_postid, postid);
            }

            json_object *content, *type, *external_1, *external_2;
            json_object_object_get_ex(posts, postid, &data);                /** .posts.<postid> */
            json_object_object_get_ex(data, "callToAction", &external_1);   /** .posts.<postid>.callToAction */
            json_object_object_get_ex(data, "domainOverride", &external_2); /** .posts.<postid>.domainOverride */
            json_object_object_get_ex(data, "media", &data);                /** .posts.<postid>.media */
            json_object_object_get_ex(data, "content", &content);           /** .posts.<postid>.media.content */
            json_object_object_get_ex(data, "type", &type);                 /** .posts.<postid>.media.type */
            const char *file_type = json_object_get_string(type);
            const char *url = json_object_get_string(content);
            if (!external_1 && !external_2 && file_type && !strcmp(file_type, "image") && url) {
                CURLUcode url_code;
                CURLU *url_info = curl_url();
                url_code = curl_url_set(url_info, CURLUPART_URL, url, 0L);
                if (0 == url_code) {
                    struct urls *url_node = malloc(sizeof(*url_node));
                    url_node->url = url_info;
                    url_node->next = urls;
                    urls = url_node;
                } else {
                    curl_url_cleanup(url_info);
                }
            }
        }

        json_object_put(json_data.json);
        json_data.json = NULL;
    }


    /** Phase 2: request images */
    struct requests {
        CURL *easy_curl;
        CURLM *multi_curl;
        CURLU *url_info;
        void *user_p;
        struct requests *next;
    } *requests = NULL;

    (void) requests;
    CURLM *multi_curl = curl_multi_init();
    int running = 0;
    int total = 0;
    const int THRESHOLD = 50;
    for (int done = 0; !done;) {
        for (struct urls *current = urls, *previous; current && running < THRESHOLD; ++total) {
            previous = current;

            char *path_name = NULL;
            CURLU *this_url = current->url;
            CURLUcode url_code;
            url_code = curl_url_get(this_url, CURLUPART_PATH, &path_name, 0L);
            if (url_code != 0) {
                printf("CURLUcode(%d)\n", url_code);
                curl_url_cleanup(current->url);
                this_url = NULL;
                current->url = NULL;

                current = current->next;
                urls = current;
                previous->url = NULL;
                free(previous);
                continue;
            }

            char *this_filename = NULL;
            size_t filename_len = 0;
            filename_len = snprintf(NULL, 0, "%s%s", dirname, &path_name[1]);
            this_filename = malloc(filename_len+1);
            size_t written = snprintf(this_filename, filename_len+1, "%s%s", dirname, &path_name[1]);
            if (written != filename_len)
                fprintf(stderr, "(%ld/%ld) %s\n", written, filename_len, this_filename);

            curl_free(path_name);

            char *file_p_filename = this_filename;
            int file_p_closed = 0;
            int file_p_fd = open(this_filename, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0644);
            long err = errno;

            if (file_p_fd > 0) {
                printf("opened fd (%d) for (%s)\n", file_p_fd, file_p_filename);
            } else if (err == EEXIST) {
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

            struct file_parser *file_p = malloc(sizeof(*file_p));
            file_p->fd = file_p_fd;
            file_p->closed = file_p_closed;
            file_p->filename = file_p_filename;

            CURL *this_easy = curl_easy_init();
            CURLcode easy_code;
            easy_code = curl_easy_setopt(this_easy, CURLOPT_CURLU, this_url);
            easy_code = curl_easy_setopt(this_easy, CURLOPT_NOPROGRESS, 1L);
            easy_code = curl_easy_setopt(this_easy, CURLOPT_USERAGENT, user_agent);
            easy_code = curl_easy_setopt(this_easy, CURLOPT_WRITEDATA, file_p);
            easy_code = curl_easy_setopt(this_easy, CURLOPT_WRITEFUNCTION, api_write_file_callback);
            easy_code = curl_easy_setopt(this_easy, CURLOPT_FOLLOWLOCATION, 1L);
            easy_code = curl_easy_setopt(this_easy, CURLOPT_BUFFERSIZE, 16 * 1024);
            if (easy_code != 0) {
                printf("CURLcode(%d)\n", easy_code);
            }

            struct requests *req = malloc(sizeof(*req));
            easy_code = curl_easy_setopt(this_easy, CURLOPT_PRIVATE, req);
            req->easy_curl = this_easy;
            req->multi_curl = multi_curl;
            req->url_info = this_url;
            req->user_p = file_p;
            req->next = requests;
            requests = req;

            CURLMcode multi_code;
            multi_code = curl_multi_add_handle(multi_curl, this_easy);
            if (multi_code != 0) {
                printf("CURLMcode(%d)\n", multi_code);
            }

            current = current->next;
            urls = current;
            previous->url = NULL;

            free(previous);
            ++running;
            curl_multi_perform(multi_curl, &running);
        }
        if (!urls)
            done = 1;

        printf("running (%d/%d)\n", running, total);

        int previous_running = running;
        while (previous_running == running && running > 0) {
            usleep(1000 * 5);
            curl_multi_perform(multi_curl, &running);
        }

        struct CURLMsg *m = NULL;
        int x = 0;
        m = curl_multi_info_read(multi_curl, &x);
        if (m && m->msg == CURLMSG_DONE && m->easy_handle) {
            struct requests *req;
            CURL *e = m->easy_handle;
            curl_easy_getinfo(e, CURLINFO_PRIVATE, &req);

            if (req && req->user_p) {
                struct file_parser *f = req->user_p;
                if (f) {
                    printf("(%d) done\n", f->fd);
                    if (f->fd > 0 && !f->closed)
                        close(f->fd);
                    f->closed = 1;
                    free(f->filename);
                    free(f);
                }
                if (req->url_info)
                    curl_url_cleanup(req->url_info);
                if (req->easy_curl)
                    curl_easy_cleanup(req->easy_curl);
                req->user_p = NULL;
                req->easy_curl = NULL;
                req->url_info = NULL;
            }
        }
        if (m && m->msg != CURLMSG_DONE) {
            fprintf(stderr, "CURLMsg (%d)\n", m->msg);
        }
    }

    {
        struct CURLMsg *m = NULL;
        printf("(Remaining) running (%d/%d)\n", running, total);
        while (running > 0) {
            int x = 0;
            usleep(1000 * 5);
            curl_multi_perform(multi_curl, &running);
            m = curl_multi_info_read(multi_curl, &x);
            if (m && m->msg == CURLMSG_DONE && m->easy_handle) {
                struct requests *req;
                CURL *e = m->easy_handle;
                curl_easy_getinfo(e, CURLINFO_PRIVATE, &req);

                if (req && req->user_p) {
                    struct file_parser *f = req->user_p;
                    if (f) {
                        printf("(%d) done\n", f->fd);
                        if (f->fd > 0 && !f->closed)
                            close(f->fd);
                        f->closed = 1;
                        free(f->filename);
                        free(f);
                    }
                    if (req->url_info)
                        curl_url_cleanup(req->url_info);
                    if (req->easy_curl)
                        curl_easy_cleanup(req->easy_curl);
                    req->user_p = NULL;
                    req->easy_curl = NULL;
                    req->url_info = NULL;
                }
            }
            if (m && m->msg != CURLMSG_DONE) {
                fprintf(stderr, "CURLMsg (%d)\n", m->msg);
            }
        }
    }

    struct requests *prev_req_node, *req_node = requests;
    while (req_node) {
        prev_req_node = req_node;
        req_node = req_node->next;

        struct file_parser *f = prev_req_node->user_p;
        if (f) {
            if (f->fd > 0 && !f->closed)
                close(f->fd);
            f->closed = 1;
            free(f->filename);
            free(f);
        }
        if (prev_req_node->easy_curl)
            curl_easy_cleanup(prev_req_node->easy_curl);
        if (prev_req_node->url_info)
            curl_url_cleanup(prev_req_node->url_info);
        free(prev_req_node);
    }

    struct urls *previous, *url_node = urls;
    while (url_node) {
        previous = url_node;
        url_node = url_node->next;

        if (url_node->url)
            curl_url_cleanup(previous->url);
        free(previous);
    }
    urls = url_node;

    curl_multi_cleanup(multi_curl);
    curl_easy_cleanup(easy_curl);
    curl_global_cleanup();

    exit(EXIT_SUCCESS);
}

size_t
api_write_file_callback(char *data, size_t size, size_t nmemb, void *user_p) {
    if (!user_p)
        return 0;

    struct file_parser *file_parser = user_p;

    int fd = file_parser->fd;
    int written = write(fd, data, size * nmemb);
    printf("(%d) written to %s %d/%ld\n", fd, file_parser->filename, written, size * nmemb);
    if (written < 0) {
        fprintf(stderr, "(%d) %s\n", errno, strerror(errno));
        return 0;
    }

    return written;
}

size_t
api_write_text_callback(char *data, size_t size, size_t nmemb, void *user_p) {
    if (!user_p)
        return 0;

    struct text_parser *text = user_p;
    char *cstr = text->cstr;
    size_t len = text->len;

    cstr = realloc(cstr, len + size * nmemb + 1);
    if (!cstr)
        return 0;

    strncpy(&cstr[len], data, size * nmemb);
    len += size * nmemb;
    cstr[len] = '\0';

    text->cstr = cstr;
    text->len = len;

    return size * nmemb;
}

size_t
api_write_json_callback(char *data, size_t size, size_t nmemb, void *user_p) {
    if (!user_p)
        return 0;

    enum json_tokener_error jerr;
    struct json_parser *json_parser = user_p;
    json_tokener *tok = json_parser->tok;
    if (!tok)
        tok = json_tokener_new();

    json_object *obj = json_tokener_parse_ex(tok, data, size * nmemb);
    jerr = json_tokener_get_error(tok);

    int done = 0;
    if (jerr == json_tokener_success) {
        done = 1;
    }
    else if (jerr != json_tokener_continue) {
        fprintf(stderr, "json error: tokener (%d) %s\n", jerr, json_tokener_error_desc(jerr));
        json_tokener_free(tok);
        json_parser->tok = NULL;
        return 0;
    }
    // printf("%.*s", (int)(size * nmemb), data);
    // if (done)
    //     printf("\n");
    if (done) {
        json_tokener_free(tok);
        tok = NULL;
    }

    json_parser->tok = tok;
    json_parser->json = obj;

    return size * nmemb;
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
