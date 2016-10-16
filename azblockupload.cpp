// reading an entire binary file
#include "was/storage_account.h"
#include "was/blob.h"

#include <iostream>
#include <fstream>
#include <queue>          
#include <list>
#include <vector>
#include <thread>
#include <ctime>

using namespace std;

void threadproc(int threadid, void *ptr);

/////////////////////////////////////////////////////////////////////////////
// 
class FileChunk
{
public:
	int id;                                             // seq id of the chunk to read
	unsigned long startpos;                             // offset in file where to start reading
	unsigned long length;                               // length of chunk to read from file
	int threadid;                                       // marked by the thread that pulls the piece from the queue
	bool completed;                                     // marked by the thread when completed
	unsigned long bytesread;                            // actual bytes read from file
	float seconds;                                      // time it took to send this chunk to Azure Storage
	utility::string_t block_id;                         // BlockId for Azure

public:
	FileChunk( int id, unsigned long startpos, unsigned long length)
	{
		this->id = id;
		this->startpos = startpos;
		this->length = length;
		this->bytesread = 0;
		this->completed = false;
		this->threadid = 0;
		this->seconds = (float)0;
	}
};
/////////////////////////////////////////////////////////////////////////////
//
class BlockUpload
{
public:
	int countThreads;                                       // how many threads to use for parallell upload
	unsigned long chunkSize;                                // size in bytes to send as chunks
	std::string filename;                                   // local file to upload
	utility::string_t blobName;                             // name of the blob in Azure
	std::queue<FileChunk*> queueChunks;                     // queue holding each chunk to upload that the threads pull from 
	azure::storage::cloud_storage_account storage_account;  // Azure Storage Account object
	azure::storage::cloud_blob_client blob_client;          // Azure Storage client object
	azure::storage::cloud_blob_container container;         // Azure Storage Container object
	float elapsed_secs;                                     // how long in seconds the upload took
	unsigned long total_bytes;                              // bytes uploaded
	bool verbose;

public:
	BlockUpload(int threads, unsigned long chunkSize)
	{
		this->countThreads = threads;
		this->chunkSize = chunkSize;
		total_bytes = 0;
		elapsed_secs = (float)0;
	}
	void ConnectToAzureStorage(std::string storageAccountName, std::string storageAccessKey, std::string containerName)
	{
		std::string connstr = "DefaultEndpointsProtocol=https;AccountName=" + storageAccountName + ";AccountKey=" + storageAccessKey;
		utility::string_t storage_connection_string( connstr.begin(), connstr.end() );
		utility::string_t blobContainer( containerName.begin(), containerName.end() );
		ConnectToAzureStorage(storage_connection_string, blobContainer);
	}
	void ConnectToAzureStorage( utility::string_t connection_string, utility::string_t containerName )
	{
		storage_account = azure::storage::cloud_storage_account::parse( connection_string );
		blob_client = storage_account.create_cloud_blob_client();
		container = blob_client.get_container_reference( containerName );
		container.create_if_not_exists();
	}
	// upload local file - blob will have same name as local file
	int UploadFile( std::string filename )
	{
		size_t found = filename.find_last_of("/\\");
		//std::string folder = filename.substr(0, found);
		std::string blobFilename = filename.substr(found + 1);
		return UploadFile( filename, blobFilename );
	}
	// upload local file and naming the blob
	int UploadFile(std::string filename, std::string blobFilename )
	{
		// get file size
		ifstream file( filename, ios::in | ios::binary | ios::ate );
		if (!file.is_open())
		{
			return 1;
		}

		// how large is the file?
		streampos size = file.tellg();
		file.close();

		// how many chunks?
		unsigned long chunks = (unsigned long)size / this->chunkSize + 1;
		unsigned long remaining = (unsigned long)size;
		unsigned long chunksread = 0;
		unsigned long currpos = 0;

		// list of chunks that we need to post-process each chunk after being uploaded
		std::list<FileChunk*> chunkl;

		// create chunks and push them on a queue
		while (remaining > 0)
		{
			chunksread++;
			long toread = remaining > this->chunkSize ? this->chunkSize : remaining;
			FileChunk *fc = new FileChunk(chunksread, (unsigned long)currpos, (unsigned long)toread);
			chunkl.push_back(fc);
			this->queueChunks.push(fc);
			remaining -= toread;
			currpos += toread;
		}

		this->filename = filename;
		blobName.assign( blobFilename.begin(), blobFilename.end() );
		azure::storage::cloud_block_blob blob1 = container.get_block_blob_reference( blobName );

		// start the timer for how fast we process the file
		unsigned t0 = clock();

		// create threads that process tasks in the queue
		std::list<std::thread*> vt;
		for (int n = 1; n <= countThreads; n++)
		{
			std::thread *t1 = new std::thread( threadproc, n, this );
			vt.push_back(t1);
		}

		// wait for all threads to complete
		std::list<std::thread*>::iterator itt;
		for (itt = vt.begin(); itt != vt.end(); ++itt)
		{
			(*itt)->join();
		}

		// stop the timer
		unsigned elapsed = clock() - t0;

		// create the block list vector from results
		this->total_bytes = 0;
		std::vector<azure::storage::block_list_item> vbi;
		std::list<FileChunk*>::iterator it;
		for (it = chunkl.begin(); it != chunkl.end(); ++it)
		{
			azure::storage::block_list_item *bli = new azure::storage::block_list_item((*it)->block_id);
			vbi.push_back(*bli);
			if (verbose)
				std::cout << "T" << (*it)->threadid << ": Chunk " << (*it)->id << ". Start " << (*it)->startpos << ", Length " << (*it)->length << ". Time: " << (*it)->seconds << std::endl;
			this->total_bytes += (*it)->bytesread;
			delete (*it);
		}

		// commit the block list items to Azure Storage
		blob1.upload_block_list(vbi);

		this->elapsed_secs = (float)elapsed / (float)CLOCKS_PER_SEC;

		return 0;
	} //
};
/////////////////////////////////////////////////////////////////////////////
//
void threadproc(int threadid, void *ptr)
{
	BlockUpload *blkup = (BlockUpload*)ptr;
	ifstream file(blkup->filename, ios::in | ios::binary | ios::ate);
	if (file.is_open())
	{
		azure::storage::cloud_block_blob blob = blkup->container.get_block_blob_reference(blkup->blobName);

		std::vector<uint8_t> buffer(blkup->chunkSize);
		// get the next file I/O task from hte queue and read that chunk
		while (!blkup->queueChunks.empty())
		{
			FileChunk *fc = (FileChunk*)(blkup->queueChunks.front());
			blkup->queueChunks.pop();

			// read the specified chunk from the file
			file.seekg(fc->startpos, ios::beg);
			file.read((char*)&buffer[0], fc->length);
			fc->bytesread = (unsigned long)file.gcount();

			// create Azure Block ID value
			fc->block_id = utility::conversions::to_base64(fc->id);
			auto stream = concurrency::streams::bytestream::open_istream(buffer);
			utility::string_t md5 = _XPLATSTR("");

			unsigned long t0 = clock();

			blob.upload_block(fc->block_id, stream, md5);

			fc->seconds = (float)(clock() - t0) / (float)CLOCKS_PER_SEC;
			fc->threadid = threadid;
			fc->completed = true;
		}
		file.close();
	}
}
/////////////////////////////////////////////////////////////////////////////
//
void splitpath( const string& str, std::string& folder, std::string& filename )
{
	size_t found;
	found = str.find_last_of("/\\");
	folder = str.substr(0, found);
	filename = str.substr(found + 1);
}
/////////////////////////////////////////////////////////////////////////////
//
int find_arg(const char* param, int argc, char* argv[])
{
	for( int n = 0; n <argc; n++ ) 
	{
		if (!strcmp(param, argv[n]))
			return n;
	}
	return -1;
}
/////////////////////////////////////////////////////////////////////////////
//
void print_syntax()
{
	cout <<
		"syntax: azblockupload -f localfile -c container [-rf blob-name] [-sa storage-account-name] [-sk storage-access-key] [-v] [-t N]\n\n" \
		"Copyright (c) 2016, RedBaronOfAzure\n" \
		"\n"
		"-f\tfile lon local machine to upload\n" \
		"\n" \
		"-c\tcontainer name in Azure Blob Storage\n" \
		"\n" \
		"-rf\tBlob name. If omitted, blob will have same name as local file\n" \
		"\n" 
		"-sa\tStorage Account Name. Overrides environment variable STORAGE_ACCOUNT_NAME\n" \
		"\n"
		"-sk\tStorage Access Key. Overrides environment variable STORAGE_ACCESS_KEY\n" \
		"\n"
		"-t N\tUse N number of threads to upload. N bust be 1..64. Default is 4\n" \
		"\n"
		"-v\tOutput details of uploaded chunks\n" \
		"\n"
		<< endl;
}
/////////////////////////////////////////////////////////////////////////////
//
int main(int argc, char* argv[] )
{
	unsigned long chunksize = 1024 * 1024 * 4; // KB to MB * 4
	int countThreads = 4;
	int idx;
	bool verbose = false;
	std::string localfile = "";
	std::string remotefile = "";
	std::string containerName = "";
	std::string storageAccountName = "";
	std::string storageAccessKey = "";

	if ( argc < 2 || -1 != find_arg("-?", argc, argv) || -1 != find_arg("-h", argc, argv) || -1 != find_arg("--help", argc, argv) )
	{
		print_syntax();
		return 0;
	}

	char* envvar = std::getenv("STORAGE_ACCOUNT_NAME");
	if (envvar)
		storageAccountName = envvar;

	if ( ( envvar = std::getenv("STORAGE_ACCESS_KEY") ))
		storageAccessKey = envvar;

	if (-1 != find_arg("-v", argc, argv))
		verbose = true;

	if (-1 != (idx = find_arg("-t", argc, argv)))
	{
		countThreads = std::atoi(argv[idx + 1]);
		if (countThreads < 1 || countThreads > 64)
		{
			print_syntax();
			cout << "Threads must be 1..64" << endl;
			return 2;
		}
	}

	if (-1 != (idx = find_arg("-f", argc, argv)))
	{
		localfile = argv[idx + 1];
	}
	else
	{
		print_syntax();
		cout << "Local filename must be specified" << endl;
		return 2;
	}

	if (-1 != (idx = find_arg("-c", argc, argv)))
	{
		containerName = argv[idx + 1];
	}
	else
	{
		print_syntax();
		cout << "Container name must be specified" << endl;
		return 2;
	}
	if (-1 != (idx = find_arg("-sa", argc, argv)))
	{
		storageAccountName = argv[idx + 1];
	}
	if (-1 != (idx = find_arg("-sk", argc, argv)))
	{
		storageAccessKey = argv[idx + 1];
	}

	if (-1 != (idx = find_arg("-rf", argc, argv)))
	{
		remotefile = argv[idx + 1];
	}
	else
	{
		std::string folder;
		splitpath( localfile, folder, remotefile );
	}

	std::cout << localfile << std::endl;

	// connect to Azure Storage
	BlockUpload *blkup = new BlockUpload( countThreads, chunksize );
	blkup->verbose = verbose;
	blkup->ConnectToAzureStorage( storageAccountName, storageAccessKey, containerName);

	int rc = blkup->UploadFile( localfile, remotefile );
	if ( rc != 0 )
	{
		cout << "Unable to open file";
	}
	else
	{
		// show perf timers
		unsigned long mb = blkup->total_bytes / (1024 * 1024);
		float MBs = ((float)mb / blkup->elapsed_secs);
		float mbps = MBs * 8;
		std::cout << "Threads: " << countThreads << ". ChunkSize: " << chunksize << ". Bytes: " << blkup->total_bytes << std::endl;
		std::cout << "Time: " << blkup->elapsed_secs << " seconds, " << mbps << " Mbps" << ", " << MBs << " MB/s" << std::endl;
	}

	delete blkup;

	return rc;
}
