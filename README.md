# Multithreaded-Web-Scraper

This repository contains a C++ web scraper designed to extract job listings from websites. It utilizes multithreading for efficient scraping and stores the extracted data in a SQLite database.

Features:

Scrapes job listings from websites concurrently using threads.
Respects robots.txt to avoid overloading the target website.
Extracts job titles, locations, salaries, dates, email addresses (if available), and application links.
Stores scraped data in a SQLite database for easy access and analysis.
Leverages libxml2 and CURL libraries for efficient scraping and data handling.
Includes advanced email validation using a dedicated library (choose one during setup).
Robust date standardization logic to handle various date formats encountered on websites.

Requirements:

C++ compiler (e.g. G++, GCC, Clang)
libxml2 development libraries
libcurl development libraries
sqlite3 development libraries
Chosen advanced email validation library (see Installation)

Installation:

Clone this repository: git clone https://github.com/eliasdphiri/Multithreaded-Web-Scraper.git
Install required libraries:
libxml2: Follow your system's package manager instructions (e.g., sudo apt install libxml2-dev on Ubuntu/Debian)
libcurl: Follow your system's package manager instructions (e.g., sudo apt install libcurl4-openssl-dev on Ubuntu/Debian)
sqlite3: Follow your system's package manager instructions (e.g., sudo apt install libsqlite3-dev on Ubuntu/Debian)
Email Validation Library:
Option 1 (EmailAddress): git clone https://github.com/soggyonion/EmailAddress.git && cd EmailAddress && mkdir build && cmake .. && make
Option 2 (cpp-email-validator): git clone https://github.com/open-source-parsers/cpp-email-validator.git && cd cpp-email-validator && mkdir build && cmake .. && make
Compile the code: g++ -o web_scraper main.cpp -lcurl -lxml2 -lsqlite3 (replace libraries paths if needed)

Usage:

Modify the following variables in main.cpp to match your scraping needs:

base_url: The base URL of the website to scrape.
initial_url: The starting URL for scraping within the website.
max_threads: The maximum number of threads to use for scraping.
max_depth: The maximum depth level for crawling links.
Compile the code (if not done already).

Run the scraper: ./web_scraper

Database:

The scraper creates a database file named job_listings.db in the current directory. This database stores scraped job information.

Contribution:

Feel free to contribute to this project by improving the scraping logic, adding support for new websites, or enhancing the extracted data.
