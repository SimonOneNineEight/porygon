# <img src="https://img.pokemondb.net/sprites/scarlet-violet/normal/porygon.png" alt="Porygon" style="width:60px"> Porygon Text Editor

Porygon is a lightweight terminal-based text editor written in C. It features syntax highlighting, file saving, basic text manipulation, and cursor navigation. Porygon runs in the terminal and operates in raw mode, allowing efficient editing with minimal dependencies.

## Overview

Porygon is inspired by classic terminal text editors like Vim and Nano. It is built from the ground up in C and implements basic text editing functionality, including:

- File opening and saving
- Syntax highlighting for C language
- Navigation with arrow keys and home/end/page keys
- Basic text operations like inserting, deleting, and searching

## Features

- **Raw Mode Input**: Captures keyboard input directly without the terminal’s default behavior (no echoing or line buffering).
- **Syntax Highlighting**: Supports highlighting for C files, with keywords, comments, strings, and numbers being color-coded.
- **Text Editing**: Insert, delete, and append text with support for tab stops and newlines.
- **File I/O**: Open and save text files.
- **Search Function**: Supports searching for text within the document.
- **Status Bar**: Displays the filename, total lines, and whether the file has been modified.
- **Basic Error Handling**: Error messages are printed if I/O operations fail.

## Acknowledgment

This project is heavily inspired by the [Kilo Text Editor](https://github.com/antirez/kilo) created by Salvatore Sanfilippo. Many of the core features and architecture are based on Kilo, and Porygon expands upon it with additional functionality and improvements.
