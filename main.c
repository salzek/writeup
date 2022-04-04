#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <winternl.h>

STARTUPINFOA si;
PROCESS_INFORMATION pi;


void main() {
	// Image'i alinacak dosya
	HANDLE HFile = CreateFileA(
		"C:\\Users\\Salihimar\\source\\repos\\PE_Hollowing\\Debug\\messageBox.exe",
		GENERIC_READ,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_READONLY,
		NULL);

	if (HFile == INVALID_HANDLE_VALUE) {
		printf("Dosya açýlamadý!");
		exit(EXIT_FAILURE);
	}

	DWORD fileSize = GetFileSize(HFile, NULL);

	if ( fileSize == INVALID_FILE_SIZE)
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
		"calc.exe",
		NULL,
		NULL,
		TRUE,
		0,
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
	while (1);
}
