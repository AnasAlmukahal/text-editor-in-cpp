# C++ text editor
   This is a Text Editor wrtitten in C++ for learning purposes that runs in the terminal, more specifically I used WSL(Windows Subsystem for Linux).
   It can run on both MacOS and Linux with small modifications(will explain later after the syntax handling update.)

# Features

   -Text editing(insert/delete, save).
   
   -Navigation with arrow keys.
   
   -search functionality.
   
   -handles Tab.
   
   -status bar and status message with Short-cuts.

   -Syntax Highlighting.

   -Filetype Detection.
   
   # Before you install:
   
   be sure to have on your machine the following or an equivalent:
   
   -GCC or any C++ compiler.
   
-Make(optional if you want to use Makefile for compiling).

# Installation

   -Clone repository or click the install by pressing the Code green button then click on "download ZIP".
   
   -if you have git installed, use this command:
      ```
      git clone https://github.com/AnasAlmukahal/text-editor-in-cpp
      ```
   change directiory:
   ```cd kilo-editor```
   
   -If you don't prefer to install Make, compile manually by the following command:
   ```g++ -o kilo kilo.cpp```
   
run the command ./kilo

# useful Shortcuts

-**Ctrl-Q**: Quit the editor
   
   -**Ctrl-S**: Save the file
   
   -**Ctrl-F**: Find a string in the file
   
   -**Arrow keys**: Move the cursor
   
   -**Home_button**: Move the cursor to the beginning of the line
   
   -**End_button**: Move the cursor to the end of the line
   
   -**Page Up**: Move the cursor up one page
   
   -**Page Down**: Move the cursor down one page
   
   -**Backspace**: Delete the character before the cursor
   
   -**Delete_button**: Delete the character under the cursor
   
-**Enter**: Insert a newline

# **Syntax Highlighting**:

   -The syntax highlighting is as follows:
   
   -numbers: red, if in search: blue.
      
   -strings: magenta.
      
   -comments: cyan. 

# **Filetype Detection**:
   -As of now, the Text Editor's Syntax highlighting works for C,C++ and .h files.
