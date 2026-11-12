#pragma once
#include <charconv>
#include <cctype>
#include <string>
#include <vector>
namespace CommandArgs {
inline bool Parse(const std::string& input,std::vector<std::string>& out,std::string& error){
 out.clear();error.clear();
 if(input.size()>4096 || input.find_first_of("\r\n")!=std::string::npos || input.find('\0')!=std::string::npos){error="Command must be one line, at most 4096 bytes";return false;}
 size_t i=0;
 while(i<input.size()){
  while(i<input.size() && std::isspace((unsigned char)input[i]))++i;
  if(i==input.size())break;
  if(out.size()>=64){error="Too many command arguments";return false;}
  std::string word;char quote=0;
  while(i<input.size()){
   char c=input[i];
   if(!quote && std::isspace((unsigned char)c))break;
   if(c=='"' || c=='\''){
    if(!quote){quote=c;++i;continue;}
    if(quote==c){quote=0;++i;continue;}
   }
   if(c=='\\' && i+1<input.size()){
    char next=input[i+1];
    if(next=='\\' || next=='"' || next=='\'' || (!quote && std::isspace((unsigned char)next))){word+=next;i+=2;continue;}
   }
   word+=c;++i;
  }
  if(quote){error="Unclosed quote; put two-word names inside matching quotes";out.clear();return false;}
  out.push_back(std::move(word));
 }
 return true;
}
inline bool Count(const std::string& text,int& value){
 int parsed=0;auto result=std::from_chars(text.data(),text.data()+text.size(),parsed);
 if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || parsed<1 || parsed>100000)return false;
 value=parsed;return true;
}
}
