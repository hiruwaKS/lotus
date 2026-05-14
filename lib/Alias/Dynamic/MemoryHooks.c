#include "Alias/Dynamic/LogRecord.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static FILE* logFile = NULL;  ///< File handle for the binary log file

/// Constructs the full path to the log file in the given directory
static char* getLogFileName(const char* dirName)
{
	const char* logName = "pts.log";
	int size = strlen(logName) + strlen(dirName) + 2;
	char* fileNameStr = (char*)malloc(size);
	strcpy(fileNameStr, dirName);
	strcat(fileNameStr, "/");
	strcat(fileNameStr, logName);
	return fileNameStr;
}

/// Prints an error message and exits the program
static void panic(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (logFile != NULL)
		fclose(logFile);
	exit(-1);
}

/// Opens the log file for binary writing in the specified directory
static void openLogFile(const char* dirName)
{
	assert(logFile == NULL);
	char* logFileName = getLogFileName(dirName);
	logFile = fopen(logFileName, "wb");
	if (logFile == NULL)
		panic("Log file \'%s\' open failed.\n", logFileName);
	free(logFileName);
}

/// Writes binary data to the log file
static void writeData(void* data, size_t size)
{
	size_t numBytesWritten = fwrite(data, size, 1, logFile);
	if (numBytesWritten != 1)
		panic("Log write error\n");
}

static void hookInit();

/// Writes a log record to the file in binary format
static void writeLogRecord(struct LogRecord* rec)
{
	if (!logFile) hookInit();
	assert(logFile != NULL && rec != NULL);
	char type = rec->type;
	writeData(&type, sizeof(char));
	switch (rec->type)
	{
		case TAllocRec:
			writeData(&rec->allocRecord.type, sizeof(char));
			writeData(&rec->allocRecord.id, sizeof(unsigned));
			writeData(&rec->allocRecord.address, sizeof(void*));
			break;
		case TPointerRec:
			writeData(&rec->ptrRecord.id, sizeof(unsigned));
			writeData(&rec->ptrRecord.address, sizeof(void*));
			break;
		case TEnterRec:
			writeData(&rec->enterRecord.id, sizeof(unsigned));
			break;
		case TExitRec:
			writeData(&rec->exitRecord.id, sizeof(unsigned));
			break;
		case TCallRec:
			writeData(&rec->callRecord.id, sizeof(unsigned));
			break;
		default:
			panic("Illegal record type\n");
	}
}

/// Closes the log file (called at program exit)
extern void HookFinalize()
{
	assert(logFile != NULL);
	fclose(logFile);
}

/// Initializes logging: creates log directory and opens the log file.
/// Uses LOG_DIR environment variable if set, otherwise defaults to "log".
static void hookInit()
{
	const char* logDirName = "log";
	const char* logDirEnv = getenv("LOG_DIR");
	if (logDirEnv != NULL)
		logDirName = logDirEnv;

	int r = mkdir(logDirName, 0755);
	if (r == -1 && errno != EEXIST)
		panic("Log directory \'%s\' creation failed.\n", logDirName);
	openLogFile(logDirName);
	assert(logFile != NULL);
	atexit(HookFinalize);
}

/// Logs a memory allocation event (global, stack, or heap)
extern void HookAlloc(char ty, unsigned id, void* addr)
{
	struct LogRecord record;
	memset(&record, 0, sizeof(record));
	record.type = TAllocRec;
	record.allocRecord.type = ty;
	record.allocRecord.id = id;
	record.allocRecord.address = addr;
	//printf("[ALLOC] %d %p\n", ty, addr);
	writeLogRecord(&record);
}

/// Logs main() function arguments (argv and envp) as global allocations
extern void HookMain(int argvId, char** argv, int envpId, char** envp)
{
	HookAlloc(1, argvId, argv);
	if (envp != NULL && envpId != 0)
		HookAlloc(1, envpId, envp);
}

/// Logs a pointer assignment (pointer ID -> target address)
extern void HookPointer(unsigned id, void* addr)
{
	struct LogRecord record;
	record.type = TPointerRec;
	record.ptrRecord.id = id;
	record.ptrRecord.address = addr;
	//printf("[PTR] %d %p\n", id, addr);
	writeLogRecord(&record);
}

/// Logs a function entry event
extern void HookEnter(unsigned id)
{
	struct LogRecord record;
	record.type = TEnterRec;
	record.enterRecord.id = id;
	//printf("[ENTER] %d\n", id);
	writeLogRecord(&record);
}

/// Logs a function exit event
extern void HookExit(unsigned id)
{
	struct LogRecord record;
	record.type = TExitRec;
	record.exitRecord.id = id;
	//printf("[EXIT] %d\n", id);
	writeLogRecord(&record);
}

/// Logs a function call event
extern void HookCall(unsigned id)
{
	struct LogRecord record;
	record.type = TCallRec;
	record.callRecord.id = id;
	//printf("[CALL] %d\n", id);
	writeLogRecord(&record);
}
