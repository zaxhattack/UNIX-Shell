#include <iostream>
#include <sys/wait.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
using namespace std;

//output to file: >
//input from file: <
enum Type {s, cd, p, rin, rout, e}; //what is the type of command (standard, current directory, pipe, redirect in, redirect out, echo)
enum Domain {fg, bg}; //is the command run in the foreground or background

//some global variables we will use
int child;
Type type = s;
Domain domain = fg;

//removes all spaces from a command, storing the actual strings in a vector. This allows a user to spam as many spaces as they want bewteen commands and not affect it
vector<string> remove_spaces(string input){
	vector<string> splitted; //output
	char delimiter = ' '; //splitting by this char
	string buff = input; //temporary buffer we can edit all we want without affecting input
	while (input.size()){
		//if the first position of the vector indicates this is an echo command, set the type to echo
		if(splitted.size() == 1){
			if(splitted[0] == "echo")
				type = e;
		}
		size_t pos = buff.find_first_of(delimiter); //postion of the next space
		if (pos != string::npos){ //if there is another space
			string part = buff.substr(0, pos); //set a temporary string as substirng up to the space of buff
			string temp = buff.substr(pos+1, buff.size()-1); //another temportary string to store the the rest of the string we will try to split again
			buff = temp; //set buff to temp so the next time loop around we will be splitting temp
			//if the current substring has length zero, don't push it to the vector unless the command in question is echo
			if(part.length() > 0 || type == e)
				splitted.push_back(part);
		}
		else{ //if there are no more space, push back buff (the remainder), break and return
			splitted.push_back (buff);
			break;
		}
	}
	return splitted;
}

//searches a vector for a specified string. Used for finding if a command has a |, <, or >
bool find_string(vector<string> input, string delimiter){
	for (int i = 0; i < input.size(); ++i){ //iterate through the vector
		if(input[i] == delimiter) //if we find an instance of the delimiter, return true
			return true;
	}
	return false; //if we never found one, return false
}

//splits a vector into a vector of vectors based on a delimiter. Used for splitting between |, <, and >. This actually splits by every instance of the delimiter, I used it initally but not anymore
vector<vector<string>> vector_split(vector<string> input, string delimiter){
	vector<vector<string>> output;

	//these two ints help keep track of where we are in the original array as we traverse it
	int end = input.size();
	int tracking = 0;
	for (int i = 0; i < input.size(); ++i){ //loop through the input array
		if(input[i] == delimiter){ //if we find an instance of the delimiter, cut out that chunk and push it into output
			vector<string> temp;
			for (int j = tracking; j < i; ++j){
				temp.push_back(input[j]);
			}
			output.push_back(temp);
			tracking = i+1;
			end = i+1;
		}
	}

	if(tracking == 0){ //if we never found an instace, push the whole original input to output to avoid errors
		output.push_back(input);
	}else{ //if not, add the last portion of the input left behind to output and return
		vector<string> temp;
		for (int i = end; i < input.size(); ++i){
			temp.push_back(input[i]);
		}
		output.push_back(temp);
	}

	return output;
}

//splits a vector into a vector of two vectors, the first being containing the text before the delimiter and the second containing the text after the delimiter (which could also contain more instances of the delimiter, such as a multiple pipe command).
vector<vector<string>> vector_split_once(vector<string> input, string delimiter){
	vector<vector<string>> output;

	//these two ints help keep track of where we are in the original array as we traverse it
	int end = input.size();
	int tracking = 0;
	for (int i = 0; i < input.size(); ++i){ //loop through the input array
		if(input[i] == delimiter){ //if we find an instance of the delimiter, cut out that chunk and push it into output
			vector<string> temp;
			for (int j = tracking; j < i; ++j){
				temp.push_back(input[j]);
			}
			output.push_back(temp);
			tracking = i+1;
			end = i+1;
			break; //break all the way out of the loops once we process the first instance of the delimiter
			break;
		}
	}

	if(tracking == 0){ //if we never found an instace, push the whole original input to output to avoid errors
		output.push_back(input);
	}else{ //if not, add the last portion of the input left behind to output and return
		vector<string> temp;
		for (int i = end; i < input.size(); ++i){
			temp.push_back(input[i]);
		}
		output.push_back(temp);
	}

	return output;
}

//main code for executing a given command
void exec_command(vector<string> command){

	command[0] = "/bin/" + command[0]; //adding /bin/ to the name of the executable allows execv to find it in the correct linux directory

	//create the args char array for exec
	char* args[command.size()];

	for (int i = 0; i < command.size(); ++i){
		char* temp = new char;
		strcpy(temp, (command[i]).c_str());
		args[i] = temp;
	}

	args[command.size()] = NULL;
		
	//if it is indicated to run in the background, treat it as such
	if(domain == bg){
		int pid = fork();
		if (pid == 0) {  // child
			execv(args[0],args); //execute it
		}else{  // parent
			waitpid(pid, &child, WNOHANG); //non-blocking wait
		}
	}else{ //run in the foreground
		if (fork() == 0) {  // child
			execv(args[0],args); //execute it
		}else {  // parent
			wait(&child); //wait for the exec to finish
		}
	}
}

//same as the above code but stripped to the bone (mainly for exec_pipe_command to use)
void simple_exec_command(vector<string> command){
	int status;
	command[0] = "/bin/" + command[0]; //adding /bin/ to the name of the executable allows execv to find it in the correct linux directory

	//if it is indicated to run in the background, treat it as such
	char* args[command.size()];
	for (int i = 0; i < command.size(); ++i){
		char* temp = new char;
		strcpy(temp, (command[i]).c_str());
		args[i] = temp;
	}

	args[command.size()] = NULL;

	execv(args[0],args); //execute it (we don't deal with all the waiting or anything like with)
}

//main code for executing a pipe command with two commands on either side of the pipe
void exec_pipe_command(vector<string> input){
	vector<vector<string>> commands = vector_split_once(input, "|"); //split the input vector by the first |

	//create the pipe
	int fds[2];
	pipe(fds);

	if(domain == bg){ //if its a background process
		int pid = fork();
		if(pid == 0){ //left comnmnand
			dup2 (fds[1],1); //overwrite the output stream with fds[1]
			close(fds[0]); //close fds[0] (we dont use it here)
			simple_exec_command(commands[0]);
		}else{ //right command
			dup2 (fds[0],0); //overwrite the input stream with fds[0]
			close(fds[1]); //close fds[1] (we dont use it here)
			if(find_string(commands[1],"|") > 0)
				exec_pipe_command(commands[1]);

			simple_exec_command(commands[1]); //execute the RHS
			waitpid(pid, &child, WNOHANG); //wait in a non blocking manner
		}
	}else{ //if its a foreground process
		if(fork() == 0){ //left comnmnand
			dup2 (fds[1],1); //overwrite the output stream with fds[1]
			close(fds[0]); //close fds[0] (we dont use it here)
			simple_exec_command(commands[0]); //execute the LHS
		}else{ //right command
			dup2 (fds[0],0); //overwrite the input stream with fds[0]
			close(fds[1]); //close fds[1] (we dont use it here)
			if(find_string(commands[1],"|") > 0) //if there exists another | in the right hand command (the format is "LHS | RHS"), call recursively call the function with the RHS
				exec_pipe_command(commands[1]);

			simple_exec_command(commands[1]); //execute the RHS
			wait(&child); //wait for the child (LHS) to finish
		}
	}
}

//main code for executing a 
void exec_rout_command(vector<string> input){

	vector<vector<string>> commands = vector_split(input, ">"); //split the vector to get the LHS and RHS of >

	//convert the string filename to a char* for use in open()
	char* filename = new char;
	strcpy(filename, commands[1][0].c_str()); 

	int file = open(filename, O_CREAT|O_WRONLY|O_TRUNC,S_IRUSR |S_IWUSR |S_IRGRP |S_IROTH); //open the file for writing
	dup2(file, 1); //overwrite the stdout stream with the file
	simple_exec_command(commands[0]); //execute the LHS
	close(file); //close the file
}

void exec_rin_command(vector<string> input){
	vector<vector<string>> commands = vector_split(input, "<"); //split the vector to get the LHS and RHS of >

	//convert the string filename to a char* for use in open()
	char* filename = new char;
	strcpy(filename, commands[1][0].c_str());

	int file = open(filename, O_RDONLY); //opent the file for reading
	dup2(file, 0); //overwrite the stdin stream with the file
	simple_exec_command(commands[0]); //execute the LHS
	close(file); //close the file

}


int main(int argc, char *argv[]){
	//declare some variables
	char cwd[256]; //for current working directory
	char cusr[256]; //for current user
	getlogin_r(cusr, sizeof(cusr)); //set current user

	while(true){ //loop that takes commands
		//reset type and domain to be safe
		type = s;
		domain = fg;
		
		getcwd(cwd, sizeof(cwd)); //set the cwd
		//get the time
		time_t t = time(NULL);
  		tm* time = localtime(&t);
  		//print out the whole promp for input:
		printf("%s :: %s (%d/%d/%d - %d:%d:%d)\n$ ~ ", cusr, cwd, (time->tm_mon)+1, time->tm_mday, (time->tm_year)+1900, time->tm_hour, time->tm_min, time->tm_sec);
		//Get the user input
		string input;
		getline(cin,input);

		//------------------------------------------------------------------------------
		//get the input from user and split it by spaces
		vector<string> split_input = remove_spaces(input);

		//------------------------------------------------------------------------------
 		//check special conditions and domain to run in
		if(split_input[0] == "quit"){ //quit command
			break;
			break;
		}else if(split_input[0] == "cd") //current directory command
			type = cd;
		else if(find_string(split_input, "|") > 0)
			type = p;
		else if(find_string(split_input, "<") == 1)
			type = rin;
		else if(find_string(split_input, ">") == 1)
			type = rout;
		else
			type = s;
		
		//check domain, remove the & if it's background
		if(split_input[split_input.size()-1] == "&"){
			domain = bg;
			split_input.erase(split_input.end()-1);
		}else{
			domain = fg;
		}

		//handle all the actual command processing and execution
		if(type == cd){
		//------------------------------------------------------------------------------
		//handle all commands of type cd
			if (split_input.size() > 2){
				cout << "ERROR: too many args for command 'cd'" << endl;
			}else{
				string temps = split_input[1]; //temp string 
				char tempc[temps.length()+1];
				strcpy(tempc, temps.c_str());
				chdir(tempc);
			}
		}else if(type == p){
		//------------------------------------------------------------------------------
		//handle all commnads of type pipe
			if(fork()==0)
				exec_pipe_command(split_input);
			else
				wait(&child);
		}else if(type == rin){
		//------------------------------------------------------------------------------
		//handle all commnads of type redirect in
			if(fork()==0)
				exec_rin_command(split_input);
			else
				wait(&child);
		}else if(type == rout){
		//------------------------------------------------------------------------------
		//handle all commnads of type redirect out
			if(fork()==0)
				exec_rout_command(split_input);
			else
				wait(&child);
		}else{
		//------------------------------------------------------------------------------
		//handle all commnads of type standard
			exec_command(split_input);
		}
		cout << '\n';
	}
	return 0;
}