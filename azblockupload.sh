#!/bin/bash
export LD_LIBRARY_PATH="/home/cljung/cpp/casablanca/Release/build.release/Binaries:/home/cljung/cpp/azure-storage-cpp/Microsoft.WindowsAzure.Storage/build.release/Binaries"
./azblockupload $1 $2 $3 $4 $5 $6

