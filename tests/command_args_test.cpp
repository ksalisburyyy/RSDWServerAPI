#include "utils/command_args.h"
#include <cassert>
#include <iostream>
int main(){std::vector<std::string> out;std::string error;int count=0;
 assert(CommandArgs::Parse("/give \"Hero Name\" 'Iron Bar' 25",out,error));assert(out==std::vector<std::string>({"/give","Hero Name","Iron Bar","25"}));
 assert(CommandArgs::Parse("kick \"Hero \\\"Ace\\\" Name\" reason",out,error));assert(out[1]=="Hero \"Ace\" Name");
 assert(CommandArgs::Parse("kick Hero\\ Name \"\"",out,error));assert(out[1]=="Hero Name" && out[2].empty());
 assert(!CommandArgs::Parse("/kick 'unfinished",out,error));assert(!CommandArgs::Parse("/kick Hero\nother",out,error));assert(!CommandArgs::Parse(std::string(4097,'x'),out,error));
 for(const auto& text:{"0","-1","100001","2junk","999999999999999999","","+2","1.5"})assert(!CommandArgs::Count(text,count));
 assert(CommandArgs::Count("1",count) && count==1);assert(CommandArgs::Count("100000",count) && count==100000);
 std::cout<<"PASS: quoted/escaped names, empty arguments, malformed/long/multiline refusal and bounded strict item counts\n";
}
