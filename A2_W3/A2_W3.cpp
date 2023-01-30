// Header comments

#include <iostream>
#include <fstream>
#include <string>
using namespace std;

string fileContent = "";

// TODO
// Load and store a file from / to disk
void readFile(string fileName)
{
	ifstream myfile;
	myfile.open(fileName);
	// TODO: file validation

	if (myfile.is_open())
	{
		string line;
		while (getline(myfile, line))
		{
			fileContent += line + "\n";
		}
		myfile.close();
	}
	else
	{
		cout << "Unable to open file";
	}

}

void saveFile(void)
{
	ofstream fw("output.txt", std::ofstream::out);
	//check if file was successfully opened for writing
	if (fw.is_open())
	{
		fw << fileContent;
		fw.close();
	}
	else cout << "Problem with opening file";
}

// Send and receive the file


// Verify the file’s contents


// Other tasks as needed
// (don’t forget, you will have to test the file validation)