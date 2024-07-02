#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <csignal>
#include <sqlite3.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <iomanip>
#include <ctime>
#include <regex>
#include <unordered_set> // for visited URLs
#include <sstream> //for string manipulation
#include <EmailAddress.h> // Library for email validation

// Global variables
std::mutex url_mutex, file_mutex, db_mutex;
std::queue<std::pair<std::string, int>> url_queue;
const std::string base_url = "https://example.com/";
const std::string initial_url = base_url;
const int max_threads = 8;
const int max_depth = 5;
sqlite3* db = nullptr;
std::string robots_txt_content;
std::unordered_set<std::string> visited_urls;

struct JobInfo {
    std::string title;
    std::string location;
    std::string salary;
    std::string datePosted;
    std::string dueDate;
    std::string emailAddress;
    std::string applicationLink;
};

// Function prototypes
std::string normalize_url(const std::string& url);
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output);
std::vector<JobInfo> parse_html(const std::string& html, const std::string& base_url);
std::vector<std::string> extract_links(const std::string& html);
void fetch_robots_txt();
bool is_allowed(const std::string& url);
void save_to_db(const std::string& url, const std::vector<JobInfo>& jobs);
void scrape_url(CURL* curl, const std::string& url, int depth, int max_retries);
void worker(int id);
void signal_handler(int signal_num);
void log_error(const std::string& message);
// Improved data parsing using multiple formats and stringstream
std::string standardize_date(const std::string& date);
bool validate_email(const std::string& email);
std::string make_absolute_url(const std::string& base_url, const std::string& relative_url);
void initialize_db_and_log();
void cleanup_db_and_log();

std::string normalize_url(const std::string& url) {
    if (url.empty()) return "";
    if (url.find("http://") == 0 || url.find("https://") == 0) return url;
    if (url[0] == '/') return base_url + url.substr(1);
    if (url.find("://") != std::string::npos) return url;
    return base_url + url;
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    output->append((char*)contents, total_size);
    return total_size;
}

std::vector<JobInfo> parse_html(const std::string& html, const std::string& base_url) {
    std::vector<JobInfo> jobs;
    htmlDocPtr doc = htmlReadMemory(html.c_str(), html.length(), NULL, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) {
        log_error("Failed to parse HTML");
        return jobs;
    }

    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    if (!context) {
        log_error("Failed to create XPath context");
        xmlFreeDoc(doc);
        return jobs;
    }

    // Adjust these XPath expressions based on the actual structure of the website
    const char* xpathExpressions[] = {
        "//div[@class='job-listing']",
        "//div[@class='job-listing']//h2[@class='job-title']",
        "//div[@class='job-listing']//span[@class='job-location']",
        "//div[@class='job-listing']//span[@class='job-salary']",
        "//div[@class='job-listing']//span[@class='date-posted']",
        "//div[@class='job-listing']//span[@class='due-date']",
        "//div[@class='job-listing']//a[@class='email-address']",
        "//div[@class='job-listing']//a[@class='application-link']"
    };

    xmlXPathObjectPtr result = xmlXPathEvalExpression((xmlChar*)xpathExpressions[0], context);
    if (!result) {
        log_error("Failed to evaluate XPath expression");
        xmlXPathFreeContext(context);
        xmlFreeDoc(doc);
        return jobs;
    }

    if (result->nodesetval) {
        xmlNodeSetPtr nodeset = result->nodesetval;
        for (int i = 0; i < nodeset->nodeNr; i++) {
            JobInfo job;

            xmlXPathContextPtr jobContext = xmlXPathNewContext(doc);
            xmlXPathSetContextNode(nodeset->nodeTab[i], jobContext);

            for (int j = 1; j < 8; j++) {
                xmlXPathObjectPtr fieldResult = xmlXPathEvalExpression((xmlChar*)xpathExpressions[j], jobContext);
                if (fieldResult && fieldResult->nodesetval && fieldResult->nodesetval->nodeNr > 0) {
                    xmlChar* content = xmlNodeGetContent(fieldResult->nodesetval->nodeTab[0]);
                    if (content) {
                        std::string value = (char*)content;
                        switch (j) {
                            case 1: job.title = value; break;
                            case 2: job.location = value; break;
                            case 3: job.salary = value; break;
                            case 4: job.datePosted = standardize_date(value); break;
                            case 5: job.dueDate = standardize_date(value); break;
                            case 6: job.emailAddress = validate_email(value) ? value : ""; break;
                            case 7: job.applicationLink = make_absolute_url(base_url, value); break;
                        }
                        xmlFree(content);
                    }
                }
                xmlXPathFreeObject(fieldResult);
            }

            jobs.push_back(job);
            xmlXPathFreeContext(jobContext);
        }
    }

    xmlXPathFreeObject(result);
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);
    return jobs;
}

std::vector<std::string> extract_links(const std::string& html) {
    std::vector<std::string> links;
    htmlDocPtr doc = htmlReadMemory(html.c_str(), html.length(), NULL, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) {
        log_error("Failed to parse HTML for link extraction");
        return links;
    }

    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    if (!context) {
        log_error("Failed to create XPath context for link extraction");
        xmlFreeDoc(doc);
        return links;
    }

    xmlXPathObjectPtr result = xmlXPathEvalExpression((xmlChar*)"//a/@href", context);
    if (!result) {
        log_error("Failed to evaluate XPath expression for link extraction");
        xmlXPathFreeContext(context);
        xmlFreeDoc(doc);
        return links;
    }

    if (result->nodesetval) {
        xmlNodeSetPtr nodeset = result->nodesetval;
        for (int i = 0; i < nodeset->nodeNr; i++) {
            xmlNodePtr node = nodeset->nodeTab[i];
            xmlChar* href = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (href) {
                links.push_back(normalize_url((char*)href));
                xmlFree(href);
            }
        }
    }

    xmlXPathFreeObject(result);
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);
    return links;
}

void fetch_robots_txt() {
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string robots_url = base_url + "robots.txt";
        curl_easy_setopt(curl, CURLOPT_URL, robots_url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &robots_txt_content);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}

bool is_allowed(const std::string& url) {
    static bool robots_txt_fetched = false;
    if (!robots_txt_fetched) {
        fetch_robots_txt();
        robots_txt_fetched = true;
    }

    std::istringstream stream(robots_txt_content);
    std::string line;
    bool apply_rule = false;

    while (std::getline(stream, line)) {
        if (line.find("User-agent: *") != std::string::npos) {
            apply_rule = true;
        } else if (line.find("User-agent:") != std::string::npos) {
            apply_rule = false;
        } else if (apply_rule && line.find("Disallow:") != std::string::npos) {
            std::string disallowed_path = line.substr(line.find("Disallow:") + 9);
            disallowed_path.erase(0, disallowed_path.find_first_not_of(" \t"));
            if (url.find(disallowed_path) != std::string::npos) {
                return false;
            }
        }
    }
    return true;
}

void save_to_db(const std::string& url, const std::vector<JobInfo>& jobs) {
    std::lock_guard<std::mutex> lock(db_mutex);

    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d_%H-%M-%S");
    std::string timestamp = ss.str();

    std::string db_name = "jobs_" + timestamp + ".db";
    const char* db_path = db_name.c_str();
    int rc = sqlite3_open(db_path, &db);
    if (rc) {
        log_error("Cannot open database: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        return;
    }

    char* errMsg;
    std::string sql = "CREATE TABLE IF NOT EXISTS jobs ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "title TEXT NOT NULL,"
                      "location TEXT,"
                      "salary TEXT,"
                      "date_posted TEXT,"
                      "due_date TEXT,"
                      "email_address TEXT,"
                      "application_link TEXT NOT NULL);";

    rc = sqlite3_exec(db, sql.c_str(), NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log_error("SQL error: " + std::string(errMsg));
        sqlite3_free(errMsg);
    }

    for (const auto& job : jobs) {
        std::string insert_sql = "INSERT INTO jobs (title, location, salary, date_posted, due_date, email_address, application_link) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        rc = sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, job.title.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, job.location.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, job.salary.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, job.datePosted.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, job.dueDate.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 6, job.emailAddress.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 7, job.applicationLink.c_str(), -1, SQLITE_STATIC);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                log_error("SQL error: " + std::string(sqlite3_errmsg(db)));
            }
            sqlite3_finalize(stmt);
        } else {
            log_error("Failed to execute SQL statement");
        }
    }

    sqlite3_close(db);
}

void scrape_url(CURL* curl, const std::string& url, int depth, int max_retries) {
    std::string html_content;
    int retries = 0;
    while (retries < max_retries) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html_content);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            std::vector<JobInfo> jobs = parse_html(html_content, url);
            save_to_db(url, jobs);
            std::vector<std::string> links = extract_links(html_content);
            {
                std::lock_guard<std::mutex> lock(url_mutex);
                for (const auto& link : links) {
                    url_queue.push({link, depth + 1});
                }
            }
            break;
        } else {
            retries++;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

void worker(int id) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to initialize curl in worker " + std::to_string(id));
        return;
    }

    while (true) {
        std::pair<std::string, int> url_pair;
        {
            std::lock_guard<std::mutex> lock(url_mutex);
            if (!url_queue.empty()) {
                url_pair = url_queue.front();
                url_queue.pop();
            } else {
                break;
            }
        }

        if (!is_allowed(url_pair.first)) {
            continue;
        }

        scrape_url(curl, url_pair.first, url_pair.second, 3);
    }

    curl_easy_cleanup(curl);
}

void signal_handler(int signal_num) {
    cleanup_db_and_log();
    exit(signal_num);
}

void log_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(file_mutex);
    std::ofstream error_log("error.log", std::ios::app);
    if (error_log.is_open()) {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        error_log << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " Error: " << message << std::endl;
        error_log.close();
    } else {
        std::cerr << "Unable to open error log file" << std::endl;
    }
}

std::string standardize_date(const std::string& date) {
    std::regex date_format("(\\d{2})/(\\d{2})/(\\d{4})");
    std::smatch matches;
    if (std::regex_search(date, matches, date_format)) {
        std::string day = matches[1];
        std::string month = matches[2];
        std::string year = matches[3];
        return year + "-" + month + "-" + day;
    }
    return date;
}

bool validate_email(const std::string& email) {
    std::regex email_format(R"((\w+)(\.{1})*(\w*)@(\w+)(\.{1})(\w+))");
    return std::regex_match(email, email_format);
}

std::string make_absolute_url(const std::string& base_url, const std::string& relative_url) {
    if (relative_url.find("http://") == 0 || relative_url.find("https://") == 0) {
        return relative_url;
    }
    if (relative_url[0] == '/') {
        return base_url + relative_url.substr(1);
    }
    return base_url + relative_url;
}

void initialize_db_and_log() {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d_%H-%M-%S");
    std::string timestamp = ss.str();

    std::string db_name = "jobs_" + timestamp + ".db";
    const char* db_path = db_name.c_str();
    int rc = sqlite3_open(db_path, &db);
    if (rc) {
        log_error("Cannot open database: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        return;
    }

    char* errMsg;
    std::string sql = "CREATE TABLE IF NOT EXISTS jobs ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "title TEXT NOT NULL,"
                      "location TEXT,"
                      "salary TEXT,"
                      "date_posted TEXT,"
                      "due_date TEXT,"
                      "email_address TEXT,"
                      "application_link TEXT NOT NULL);";

    rc = sqlite3_exec(db, sql.c_str(), NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log_error("SQL error: " + std::string(errMsg));
        sqlite3_free(errMsg);
    }

    std::ofstream error_log("error.log", std::ios::app);
    if (!error_log.is_open()) {
        std::cerr << "Unable to open error log file" << std::endl;
    }
}

void cleanup_db_and_log() {
    sqlite3_close(db);
}

int main() {
    initialize_db_and_log();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to initialize curl in main");
        return 1;
    }

    url_queue.push({initial_url, 0});
    std::vector<std::thread> threads;
    for (int i = 0; i < max_threads; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    curl_easy_cleanup(curl);
    cleanup_db_and_log();

    return 0;
}
