#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

int main(int argc, char *argv[]) {
    // Check to make sure the run command of the file is valid
    if (argc != 3) {
        cout << "Not enough arguments, please follow the rule by <usage> + <input_file> + <timer interrupt" << endl;
        return 1;
    }

    // Prompt the name of the file and the timer interrupt
    string inputFile = argv[1];
    int timerInterrupt = atoi(argv[2]);

    // Create the pipe
    int pipeToChild[2];
    int pipeToParent[2];

    // Create pipes
    if (pipe(pipeToChild) == -1 || pipe(pipeToParent) == -1) {
        cout << "Error creating pipes" << endl;
        exit(EXIT_FAILURE);
    }

    //Identify for child or parent process
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    // This is the child process
    if (pid == 0) {
      // Create the array that consists of 2000 integer entries,
      // 0-999 for the user program, 1000-1999 for system code.
      int *mem = new int[2000]; // The () initializes all elements to zero
      ifstream file(inputFile.c_str());  // Open file with c_str()
      if (!file.is_open()) {
          cerr << "Error opening file: " << inputFile << endl;
          exit(EXIT_FAILURE);
      }
      string buf;
      int line = 0;
      int markerLine = -1;  // To keep track of the marker line

      while (getline(file, buf)) {
        // Trim leading and trailing whitespace from the line
        buf.erase(0, buf.find_first_not_of(' '));       //prefixing spaces
        buf.erase(buf.find_last_not_of(' ')+1);         //suffixing spaces

        // Check if the line is not empty and contains a number or a period
        if (!buf.empty() && (isdigit(buf[0]) || buf[0] == '.')) {
          int num;
          istringstream iss(buf);
          if (buf[0] == '.' && buf.size() > 1) {
            // Handle the dot differently
            iss.ignore(); // Ignore the dot
            iss >> num >> ws;
            if (iss.fail()) {
              cerr << "Failed to parse number from line: " << line << endl;
              cerr << "Line content: " << buf << endl;
              exit(EXIT_FAILURE);
            }
            line = num;
          } else {
            iss >> num >> ws;
            mem[line] = num;
            if (markerLine != line) { // Only increment line if it's not a marker line
              line++;
            }
            markerLine = -1; // Reset marker line
          }
        }
      }
      //close the file
      file.close();
      // Memory access and communication loop
      int PC;
      int value;
      int address;
      while (true) {
        // Read the program counter (PC) from the parent process
        read(pipeToChild[0], &PC, sizeof(PC));
        // Write command
        if (PC == -1) {
          // Read address and value from the parent process
          read(pipeToChild[0], &address, sizeof(address));
          read(pipeToChild[0], &value, sizeof(value));
          mem[address] = value;
        }
        // Read command
        else {
          // Read the instruction from the memory array at the specified address
          int instruction = mem[PC];

          // Write the instruction to the parent process
          write(pipeToParent[1], &instruction, sizeof(instruction));
        }
      }
    }
    else {
      //parent process code
      //Create Registers
      int PC=0, SP=1000, IR=0, AC=0, X=0, Y=0;
      int inputValue = 0;
      int temporarySP;

     bool kernel = false;

    //Create signal to allow write or interrupt
     int signalWrite = -1;
     int signalInterrupt = 0;
     bool signalTimer = false; //Check if it is time for a timer interrupt
     int countTimer = 0;

     while (true){
       //Timer Interrupt
       //If it is 1 is for syscall and if it is 2 is for timer interrupt.
       if (signalTimer && signalInterrupt ==0) {
         signalTimer = false;
         signalInterrupt = 2;

         //kernel mode signal
         kernel = true;

         //Stack pointer switches to system Stack
         temporarySP = SP;
         SP = 2000;

         //Save PC
         SP--;
         PC++;
         //send the write signal
         write(pipeToChild[1], &signalWrite, sizeof(signalWrite)); 
         //store it at the stack pointer
         write(pipeToChild[1], &SP, sizeof(SP)); 
         //return address 
         write(pipeToChild[1], &PC, sizeof(PC)); 

         //Save SP
         SP--;
         //send the write signal
         write(pipeToChild[1], &signalWrite, sizeof(signalWrite)); 
         //store it to SP
         write(pipeToChild[1], &SP, sizeof(SP)); 
         // return address (value we are storing)
         write(pipeToChild[1], &temporarySP, sizeof(temporarySP)); 

         //Begin executing at 999
         PC = 1000;
       }

       // fetch next instruction
       write(pipeToChild[1], &PC, sizeof(PC));
       read(pipeToParent[0], &IR, sizeof(IR));

       switch (IR) {

        case 1: //Load Value
          PC++; // increase PC for input
          //Prompt the input
          write(pipeToChild[1], &PC, sizeof(PC));
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); //read the returned inputValue
          AC = inputValue;
          break;

        case 2: //Load address
          PC++; //increase PC for input
           //Input process
          write(pipeToChild[1], &PC, sizeof(PC));
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); //fetch inputValue

          //defend against user accessing system memory
          if (inputValue>=1000 && kernel == false) {
            cout << "This accessing system address: "<<inputValue<<" in user mode " << '\n';
          }
          //ask for value at the array with address given
          write(pipeToChild[1], &inputValue, sizeof(inputValue));
          //read the value returned by memory 
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 
          AC = inputValue; //save it to the AC
          break;

        case 3: //Load the value from the address found in the given address into the AC
          PC++; // increase PC
          //ask for the inputValue
          write(pipeToChild[1], &PC, sizeof(PC)); 
          //fetch inputValue
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 

          //ask for value at the array with address given
          write(pipeToChild[1], &inputValue, sizeof(inputValue));
          //read the value returned by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 

          //avoid user accessing system memory
          if (inputValue>=1000 && kernel == false) {
            cout << "Memory violation error: accessing system address "<<inputValue<<" in user mode " << '\n';
          }
          //ask for value at the array with address given
          write(pipeToChild[1], &inputValue, sizeof(inputValue));
          //read the value returned by memory 
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 
          AC = inputValue;
          break;

        case 4: //Load the value at (address+X) into the AC
          PC++; // increase PC
          //ask for the inputValue
          write(pipeToChild[1], &PC, sizeof(PC)); 
          //fetch inputValue
          read(pipeToParent[0], &inputValue, sizeof(inputValue));

          inputValue+=X;
          //ask for value at the array with address given
          write(pipeToChild[1], &inputValue, sizeof(inputValue)); 
          //read the value returned by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 
          AC = inputValue; //save it to the AC
          break;

        case 5: //Load the value at (address+Y) into the AC
          PC++; //increase PC
          //ask for the inputValue
          write(pipeToChild[1], &PC, sizeof(PC)); 
          //fetch inputValue
          read(pipeToParent[0], &inputValue, sizeof(inputValue));
          inputValue+=Y;
          //ask for value at the array with address given
          write(pipeToChild[1], &inputValue, sizeof(inputValue)); 
          //read the value returned by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 
          //save to AC
          AC = inputValue; 
          break;

        case 6: //Load from (Sp+X) into the AC

          inputValue = SP + X;
          //ask for value at the array with address given
          write(pipeToChild[1], &inputValue, sizeof(inputValue)); 
          //read the value returned by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 
          AC = inputValue; //save it to the AC
          break;

        case 7: //Store the value in the AC to the address
          PC++;
          //ask for the inputValue
          write(pipeToChild[1], &PC, sizeof(PC)); 
          //fetch inputValue
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 
          //Signal to write
          write(pipeToChild[1], &signalWrite, sizeof(signalWrite)); 
          //send the address to which we are storing
          write(pipeToChild[1], &inputValue, sizeof(inputValue)); 
          //send the value we are storing
          write(pipeToChild[1], &AC, sizeof(AC)); 
          break;

        case 8: //AC = random integer from 1 - 100
          AC = rand() % 100 + 1;
          break;

        case 9: // Print to screen
          PC++;
          write(pipeToChild[1], &PC, sizeof(PC));
          //read the value returned by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue)); 
          if (inputValue == 1) {
            printf("%i", AC );
          }
          if (inputValue == 2) {
            printf("%c", AC );
          }
          break;

        case 10: //Add X to AC

          AC+=X;
          break;

        case 11: //Add Y to AC
          AC+=Y;
          break;

        case 12: //Subtract X from AC

          AC-=X;
          break;

        case 13: //Subtract Y from AC
          AC-=Y;
          break;

        case 14: //Copy to X

          X = AC;
          break;

        case 15: //Copy from X
          AC = X;
          break;

        case 16: //Copy to Y
          Y = AC;
          break;

        case 17: //Copy from Y
          AC = Y;
          break;

        case 18: //Copy to SP
          SP = AC;
          break;

        case 19: //Copy from SP
          AC = SP;
          break;

        case 20: //Jump to Address
          PC++;
          //ask for value at the array with address given
          write(pipeToChild[1], &PC, sizeof(PC));
          //read the value and return by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue));
          PC = inputValue-1;
          break;

        case 21: //Jump to address if AC == 0
          PC++;
          //ask for value at the array with address given
          write(pipeToChild[1], &PC, sizeof(PC));
          //read the value and return by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue));
          if (AC == 0) {
            PC = inputValue-1;
          }
          break;

        case 22: //Jump to address if AC != 0
          PC++;
          //ask for value at the array with address given
          write(pipeToChild[1], &PC, sizeof(PC));
          //read the value and return by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue));
          if (AC != 0) {
            PC = inputValue-1;
          }
          break;

        case 23: //Push return address onto stack, jump to the address

          //Get the inputValue, so we can comeback later
          PC++;
          //ask for value at the array with address given
          write(pipeToChild[1], &PC, sizeof(PC)); 
           //read the value returned by memory
          read(pipeToParent[0], &inputValue, sizeof(inputValue));

          //push return address onto stack
          SP--;
          PC++; //increment because due to param, return address will be 2 after current addess.
          //Signal to write
          write(pipeToChild[1], &signalWrite, sizeof(signalWrite)); 
          //Store at the address that we are storing in stack
          write(pipeToChild[1], &SP, sizeof(SP)); 
          //send the return address (value we are storing)
          write(pipeToChild[1], &PC, sizeof(PC)); 
          //set PC to the value that at the inputValue address minus 1
          PC = inputValue-1; 
          break;

        case 24: //Pop return address from the stack, jump to the address
          //pop return address from stack
          write(pipeToChild[1], &SP, sizeof(SP)); 

          //jump to the address
          read(pipeToParent[0], &PC, sizeof(PC)); 
          //PC++ at end
          PC--; 
          SP++;
          break;

        case 25: //Increment X
            X++;
          break;

        case 26: //Decrement X
          X--;
          break;

        case 27: //Push AC onto Stack
          SP--;
          //Signal to write
          write(pipeToChild[1], &signalWrite, sizeof(signalWrite)); 
          //Store at the address that we are storing in stack
          write(pipeToChild[1], &SP, sizeof(SP));
          //send the return address (value we are storing)
          write(pipeToChild[1], &AC, sizeof(AC)); 
          break;

        case 28: //Pop from stack into AC

          //pop return address from stack
          write(pipeToChild[1], &SP, sizeof(SP)); 
          //save to AC
          read(pipeToParent[0], &AC, sizeof(AC)); 
          //adjust stack pointer
          SP++; 
          break;

        case 29: //Perform system call
          //if we are currently in an interrupt, interrupts should be disabled
          if (signalInterrupt != 0) {
            break;
          }
          signalInterrupt = 1;



          //turn on kernel mode
          kernel = true;

          //Stack pointer switches to system 
          temporarySP = SP;
          SP = 2000;

          //Save PC onto System Stack
          SP--;
           //Signal to write
          write(pipeToChild[1], &signalWrite, sizeof(signalWrite));
          //Store at the address that we are storing in stack
          write(pipeToChild[1], &SP, sizeof(SP)); 
          //send the return address (value we are storing)
          write(pipeToChild[1], &PC, sizeof(PC)); 

          //Save SP onto System Stack
          SP--;
          //Signal to write
          write(pipeToChild[1], &signalWrite, sizeof(signalWrite)); 
          //Store at the address that we are storing in stack
          write(pipeToChild[1], &SP, sizeof(SP)); 
          //send the return address (value we are storing)
          write(pipeToChild[1], &temporarySP, sizeof(temporarySP)); 

          //Begin executing at 1500
          PC = 1499;

          break;

        case 30: // Return from system call
          //pop return address from stack
          write(pipeToChild[1], &SP, sizeof(SP));
          read(pipeToParent[0], &temporarySP, sizeof(temporarySP)); //save to SP
          SP++; //adjust stack pointer

          //pop return address from stack
          write(pipeToChild[1], &SP, sizeof(SP));
          read(pipeToParent[0], &PC, sizeof(PC)); //save to PC
          PC-=2; 
          SP++; //adjust stack pointer

          //unflag the interrupts
          if (signalInterrupt==2){ //if we are returning from a timer interrupt (denoted by 2)
            signalTimer = false;
          }
          signalInterrupt = 0;

          //reset the SP
          SP = temporarySP; 


          //user mode;
          kernel = false;


          break;

        case 50: //End Execution
          _exit(0);
          break;

        default:
          cout << "THIS: "<<IR<<" NOT A COMMAND" << '\n';
       }

       countTimer++;
       PC++;

       if (countTimer%timerInterrupt == 0){
         signalTimer = true;
       }

     }

   return 0;
  }
}
