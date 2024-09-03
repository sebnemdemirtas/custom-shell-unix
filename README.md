This is the COMP304 course Project-1 code repository for the Unix-style operating system shell, called Hshell

Implemented by: 

    Sebnem Demirtas and Mete Erdogan

Both the kernel module and the shell code can be compiled using the "make all" command with the provided makefile.
We have the following generic commands that are run with the following examples:

- hdiff:

      hdiff [<mode_flag>] <file_name1> <file_name2>

  Here, if the mode flag is -a, or not given, we compare line by line. If the mode flag is -b, bit by bit comparison is made.
  We provide the compare1.txt and compare2.txt files to experiment with the hdiff command.

- regression
  
      regression <file_name> [<polynomial_indicator>] [<degree>]
  
  Description of the parameters:
  - file_name: The name of the text file containing the data.
  - polynomial_indicator: Optional. If -p is written, then polynomial regression is performed. If not indicated, then linear regression is performed.
  - degree: If type is indicated as polynomial, then the degree of the polynomial regression should also be entered as the third argument.

  We provide the input.txt file to experiment with the regression command.

- textify
  
      textify <filename> <mode> [additional arguments]

  Example usages are:
  - textify <txtfile_name> -count_letters
  - textify <txtfile_name> -count_words
  - textify <txtfile_name> -count_specific_word [<search_word>]
  - textify <txtfile_name> -change_words [<old_word>] [<new_word>]

- psvis
  
      psvis <process_id> <filename>    

  The filename should be provided without the ".txt" or ".png" extensions. The output will be saved both as a txt and png file


  

