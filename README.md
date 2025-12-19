## Project Overview

This project aims to design and implement a bilingual translation server in C that translates words between English and French. 
The project is divided into three incremental versions, each adding more complexity and introducing new concepts from operating systems and systems programming.

## Version 1

1. Dictionary Files: The server reads all files in a specified folder at startup to build a dictionary of English-French word pairs. Each file should contain word pairs in the format: EnglishWord;FrenchTranslation
2. Dynamic Updates: If new files are added to the folder while the server is running, the server detects these files and updates the dictionary. The server should periodically scan the target directory using functions such as opendir() and readdir() to detect new files.
3. Server: The dictionary server consists of a simple server, which receives signals, choose a random word from the stored array and prints to the screen either English-to-French or French-to-English translations based on the request type.
4. Client: The client program, which randomly choose one of the two signals “SIGUSR1” and “SIGUSR2” and sends the signal to the server, specifying the translation direction. The client sends `SIGUSR1` for English→French and `SIGUSR2` for French→English translation.

## Version 2

Array that contains all words is stored using a shared memory. The second version of translation server entails:
1. Translation Writer: A thread that reads English-French pairs from all files in the folder, categorizing them by translation direction (English-to-French or French-to-English), and writes them to a message queue. Each message includes the word pair and the request type.
2. Translation Reader: Another thread that reads translations from the message queue, based on request type, and store the pair in the shared array.
3. Threads: Implement both the reader and writer as threads to handle concurrent translation requests.
Dictionary Management: The writer periodically scans the folder for new dictionary files and updates the translation pairs in real time.

## Version 3

1. Unified Server: Combines the functionality of Versions 1 and 2 into a single server that: - Listens for translation requests from the client (English-to-French or French-to-English). - Periodically checks the folder for new dictionary files, updating the dictionary
dynamically. - Responds to client translation requests using the latest dictionary data.
2. Dynamic Reloading: When a requested translation is not found, the server should trigger a dictionary reload (e.g., by re-scanning the directory and updating the shared array before responding).”



