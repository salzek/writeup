#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <winternl.h>

STARTUPINFOA si;
PROCESS_INFORMATION pi;


void main() {
	// Image'i alinacak dosya
	HANDLE HFile = CreateFileA(
		"C:\\Users\\salzE\\Desktop\\Injections\\test\\test.exe",
		GENERIC_READ,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_READONLY,
		NULL);

	if (HFile == INVALID_HANDLE_VALUE) {
		printf("Dosya acilamadi!");
		exit(EXIT_FAILURE);
	}

	DWORD fileSize = GetFileSize(HFile, NULL);

	if (fileSize == INVALID_FILE_SIZE)
	{
		printf("Dosya okunamadi!");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("Dosyadan %d byte okundu.\n", fileSize);
	}

	LPVOID allocatedMemAddr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)fileSize);

	if (allocatedMemAddr == NULL)
	{
		printf("Alan tahsis edilemedi!");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("Heap uzerinde yeterli alan tahsis edildi.\n");
	}

	DWORD numberOfBytesRead;

	if (ReadFile(HFile, allocatedMemAddr, fileSize, &numberOfBytesRead, NULL) == FALSE)
	{
		printf("Dosyadan okuma yapilamadi!");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("Heap uzerinde tahsis edilen alana %d byte aktarildi\n", numberOfBytesRead);
	}


	if (CreateProcessA(
		NULL,
		"c:\\windows\\syswow64\\notepad.exe",
		NULL,
		NULL,
		TRUE,
		4,
		NULL,
		NULL,
		&si, &pi) == 0) {
		printf("Process olusturulamadi!");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("pid number: %d", pi.dwProcessId);
	};
	
	typedef NTSTATUS(NTAPI* NtQueryInformationProcessPtr)(
		IN HANDLE ProcessHandle,
		IN PROCESSINFOCLASS ProcessInformationClass,
		OUT PVOID ProcessInformation,
		IN ULONG ProcessInformationLength,
		OUT PULONG ReturnLength OPTIONAL);

	PROCESS_BASIC_INFORMATION status;

	HMODULE Hntdll = LoadLibraryA("ntdll.dll");

	NtQueryInformationProcessPtr NtQueryInformationProcess = (NtQueryInformationProcessPtr)GetProcAddress(Hntdll, "NtQueryInformationProcess");
	DWORD returnLenght = 0;
	NtQueryInformationProcess(pi.hProcess, ProcessBasicInformation, &status, sizeof(PROCESS_BASIC_INFORMATION), &returnLenght);
	printf("return lenght:%d\n", returnLenght);
	DWORD pebImageBaseOffset = (DWORD)status.PebBaseAddress + 8;
	printf("%d\n%d", (DWORD)status.PebBaseAddress, status.PebBaseAddress);
	 
	//printf("%d\n%d", pebImageBaseOffset, *((LPDWORD)status.PebBaseAddress + 8));
	/*
	LPVOID buff = 0;
	SIZE_T bytesRead = NULL;
	HANDLE destProcess = pi.hProcess;
	ReadProcessMemory(destProcess, ((DWORD)status.PebBaseAddress + 8), &buff, 4, &bytesRead);
	*/
}
