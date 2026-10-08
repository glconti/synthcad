#include "agent_entry.h"
int main(int argc,char** argv){
  auto args=synthcad::ProcessArguments(argc,argv);
  const auto executable=args.front();args.erase(args.begin());
  return synthcad::RunAgentCli(args,executable);
}
