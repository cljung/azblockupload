# Azure Block Upload client in C++

This sample code uses Azure's C++ Storage APIs to upload a file using the <a href="https://msdn.microsoft.com/en-us/library/azure/dd135726.aspx">Block List API's</a> to upload a file to Azure Storage. It creates a set of background threads that upload a file to a blob container.
The code uses Microsoft's <a href="https://github.com/Azure/azure-storage-cpp" target="_blank">C++ Azure Storage Account library</a> and compiles on Windows, Linux and Mac.

## Building the sample on Linux
Building this sample on an Ubuntu Linux is explanied in the README.md in <a href="https://github.com/Azure/azure-storage-cpp" target="_blank">azure-storage-cpp</a> github.
You need to git clone and build something called <a href="https://github.com/microsoft/cpprestsdk">Casablanca and azure-storage-cpp</a>. Casablanca is a C++ REST API client implementation by Microsoft which helps you build clients for any REST based solution. You will find Casablanca referenced from azure-storage-cpp.
WARNING - the instructions for some reason builds Casablanca as a debug build and azure-storage-cpp as a release build. I did a pure release build of them both.

In order to build and run this sample, you need to make sure that the ROOTDIR definition is currect. The ROOTDIR variable is the parent folder to azkvault, Casablanca and azure-storage-cpp.
The makefile grabs the parent folder of the current folder and uses as ROOTDIR.
<pre>
<code>
PWD=$(shell pwd)
ROOTDIR=$(shell dirname $(PWD))
</code>
</pre>
Depending on what you call the folders for your Casablanca and azure-storage-cpp builds you may need to change the CASABLANCA_BINDIR and the AZURECPP_BINDIR too. I called them build.release

## Building the sample on Windows
Just open the solution in Visual Studio (2015) and rebuild. It will download the NuGet package wastorage as part of the build.

## Running the sample
Run the code with no arguments to see syntax. To upload a file, run with the below arguments
<pre>
<code>
set/export STORAGE_ACCOUNT_NAME=account-name
set/export STORAGE_ACCESS_KEY=key
./azblockupload -f local-file -c container-name
</code>
</pre>

