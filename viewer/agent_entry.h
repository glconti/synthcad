#pragma once
#include <string>
#include <vector>
#include <filesystem>
namespace synthcad {
std::vector<std::string> ProcessArguments(int argc,char** argv);
bool IsAgentCommand(const std::vector<std::string>& arguments);
int RunAgentCli(const std::vector<std::string>& arguments,const std::string& executable);
void WriteOutputFile(const std::filesystem::path& path,const std::string& bytes,bool replace);
}
