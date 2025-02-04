#include "windows.h"
#include "vector"
#include <tchar.h>
#include <GL/freeglut.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include "PluginConfigApi.h"
#include <simpleini.h>

#include <detours.h>
#include "framework.h"
#pragma comment(lib, "detours.lib")

#include "101.h"
#include "130.h"
#include "301.h"
#include "600.h"

unsigned short game_version = 101;

void InjectCode(void* address, const std::vector<uint8_t> data);
void ApplyCustomPatches(std::wstring CPATCH_FILE_STRING);
void ApplyPatches();

const LPCWSTR CONFIG_FILE = L".\\config.ini";
const LPCWSTR PATCHES_FILE = L".\\patches.ini";

HMODULE *hModulePtr;

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		hModulePtr = &hModule;
	    if (*(char*)0x004ed611 == (char)0x8b) game_version = 130;
		else if (*(char*)0x007B1210 == (char)0x83) game_version = 301;
		else if (*(char*)0x004592CC == (char)0x74) game_version = 600;
		ApplyPatches();
	}
	return TRUE;
}

void ApplyPatches() {
	std::string version_string = std::to_string(game_version);
	version_string.insert(version_string.begin() + 1, '.');
	std::cout << "[Patches] Game version " + version_string << std::endl;

	bool create = false;
	if (!std::filesystem::exists(PATCHES_FILE))
	{
		create = true;
		std::ofstream outfile(PATCHES_FILE);
		std::ifstream in(".\\patches.dva", std::ifstream::ate | std::ifstream::binary);
		outfile << "# ONLY FOR ADVANCED USERS" << std::endl << "[patches]" << std::endl;
		outfile.close();
	}

	CSimpleIniA ini;
	ini.LoadFile(PATCHES_FILE);

	const Patch* patch_ptr = NULL;

	switch (game_version)
	{
	case 101:
		patch_ptr = patches_101;
		break;
	case 130:
		patch_ptr = patches_130;
		break;
	case 301:
		patch_ptr = patches_301;
		break;
	case 600:
		patch_ptr = patches_600;
		break;
	}

	if (patch_ptr == NULL) abort();

	while (patch_ptr->Address != 0x0)
	{
		if (create) ini.SetBoolValue("patches", patch_ptr->Name, true);
		else if (!create && !ini.GetBoolValue("patches", patch_ptr->Name)) { patch_ptr += 1; continue; }
		InjectCode(patch_ptr->Address, patch_ptr->Data);
		patch_ptr += 1;
	}
	if (ini.SaveFile(PATCHES_FILE)) MessageBoxA(NULL, "Saving Patches.ini failed", "Patches", NULL);

	std::cout << "[Patches] Patches loaded" << std::endl;

	std::cout << "[Patches] Reading custom patches...\n";
	try {
		for (std::filesystem::path p : std::filesystem::directory_iterator("../patches"))
		{
			std::string extension = std::filesystem::path(p).extension().string();
			if ((extension == ".p" || extension == ".P" || extension == ".p2" || extension == ".P2") &&
				GetPrivateProfileIntW(L"plugins", std::filesystem::path(p).filename().c_str(), TRUE, CONFIG_FILE))
			{
				std::cout << "[Patches] Reading custom patch file: " << std::filesystem::path(p).filename().string() << std::endl;
				ApplyCustomPatches(std::filesystem::path(p).wstring());
			}
		}
	}
	catch (const std::filesystem::filesystem_error& e) {
		std::cout << "[Patches] File system error " << e.what() << " " << e.path1() << " " << e.path2() << " " << e.code() << std::endl;
	}
}

void InjectCode(void* address, const std::vector<uint8_t> data)
{
	const size_t byteCount = data.size() * sizeof(uint8_t);

	DWORD oldProtect;
	VirtualProtect(address, byteCount, PAGE_EXECUTE_READWRITE, &oldProtect);
	memcpy(address, data.data(), byteCount);
	VirtualProtect(address, byteCount, oldProtect, nullptr);
}

void ApplyCustomPatches(std::wstring CPATCH_FILE_STRING)
{
	LPCWSTR CPATCH_FILE = CPATCH_FILE_STRING.c_str();
	std::ifstream fileStream(CPATCH_FILE_STRING);

	if (!fileStream.is_open())
		return;

	std::string line;

	// check for BOM
	std::getline(fileStream, line);
	if (line.size() >= 3 && line.rfind("\xEF\xBB\xBF", 0) == 0)
		fileStream.seekg(3);
	else
		fileStream.seekg(0);

	while (std::getline(fileStream, line))
	{
		if (line[0] == '#')
		{
			std::cout << "[Patches]" << line.substr(1, line.size() - 1) << std::endl;
			continue;
		}
		if (line == "IGNORE") break;
		if (line.find(':') == std::string::npos || (line[0] == '/' && line[1] == '/')) continue;

		std::vector<std::string> commentHSplit = SplitString(line, "#");
		std::vector<std::string> commentDSSplit = SplitString(commentHSplit[0], "//");
		std::vector<std::string> colonSplit = SplitString(commentDSSplit[0], ":");
		if (colonSplit.size() != 2) continue;
		bool echo = true;
		if (colonSplit[0].at(0) == '@')
		{
			echo = false;
			colonSplit[0].at(0) = ' ';
		}
		long long int address;
		std::istringstream iss(colonSplit[0]);
		iss >> std::setbase(16) >> address;
		if (address == 0) std::cout << "[Patches] Custom patch address wrong: " << std::hex << address << std::endl;

		if (colonSplit[1].at(0) == '!')
		{
			if (echo) std::cout << "[Patches] Patching: " << std::hex << address << ":!";
			std::vector<std::string> fullColonSplit = SplitString(line, ":");
			for (int i = 1; i < fullColonSplit[1].size(); i++)
			{
				unsigned char byte_u = fullColonSplit[1].at(i);
				if (byte_u == '\\' && i < fullColonSplit[1].size())
				{
					switch (fullColonSplit[1].at(i + 1))
					{
					case '0':
						byte_u = '\0';
						i++;
						break;
					case 'n':
						byte_u = '\n';
						i++;
						break;
					case 't':
						byte_u = '\t';
						i++;
						break;
					case 'r':
						byte_u = '\r';
						i++;
						break;
					case 'b':
						byte_u = '\b';
						i++;
						break;
					case 'a':
						byte_u = '\a';
						i++;
						break;
					case 'f':
						byte_u = '\f';
						i++;
						break;
					case 'v':
						byte_u = '\v';
						i++;
						break;
					case '\\':
						byte_u = '\\';
						i++;
						break;
					}
				}
				if (echo) std::cout << byte_u;
				std::vector<uint8_t> patch = { byte_u };
				InjectCode((void*)address, patch);
				address++;
			}
			if (echo) std::cout << std::endl;
		}
		else
		{
			std::vector<std::string> bytes = SplitString(colonSplit[1], " ");
			if (bytes.size() < 1) continue;

			std::string comment_string = "";
			int comment_counter = 0;
			if (commentHSplit.size() > 1)
			{
				bool ignore = 1;
				for (std::string comment : commentHSplit)
				{
					if (ignore)
					{
						ignore = 0;
						continue;
					}
					comment_string = comment_string + comment;
				}
			}

			if (echo) std::cout << "[Patches] Patching: " << std::hex << address << ":";
			for (std::string bytestring : bytes)
			{
				int byte;
				std::istringstream issb(bytestring);
				issb >> std::setbase(16) >> byte;
				unsigned char byte_u = byte;
				if (echo)
				{
					std::cout << std::hex << byte << " ";
					if (comment_counter < comment_string.length())
					{
						std::cout << "(" << comment_string.at(comment_counter) << ") ";
						comment_counter++;
					}
				}
				std::vector<uint8_t> patch = { byte_u };
				InjectCode((void*)address, patch);
				address++;
			}
			if (echo) std::cout << std::endl;
			else if (comment_string.length() > 0)
			{
				std::cout << "[Patches]";;
				if (comment_string.at(0) != ' ') std::cout << ' ';
				std::cout << comment_string << std::endl;
			}
		}
	}

	fileStream.close();
}

using namespace PluginConfig;

// Note for developers
// These do jack until theres an actual UI

extern "C" __declspec(dllexport) LPCWSTR GetPluginName(void)
{
	return L"Patches";
}

extern "C" __declspec(dllexport) LPCWSTR GetPluginDescription(void)
{
	return L"Applies patches/tweaks to the game before it starts.\nThis plugin is required.";
}
