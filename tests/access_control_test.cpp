#include "net/access_control.h"
#include "utils/json.h"
#include <cassert>
#include <iostream>
int main(){
 Net::IpFilter f;f.Configure("");assert(f.Allows(0x7f000001));f.Configure("not-an-ip");assert(!f.Allows(0x7f000001));
 f.Configure("127.0.0.1/not-a-prefix");assert(!f.Allows(0x01020304));
 f.Configure("127.0.0.1,10.0.0.0/8");assert(f.Allows(0x7f000001));assert(f.Allows(0x0a000001));assert(!f.Allows(0x01020304));
 std::string text;int count=0;assert(Json::GetString("{\"name\":\"A \\u263a } name\"}","name",text));assert(text=="A ☺ } name");
 assert(!Json::GetString("{\"nested\":{\"name\":\"wrong\"}}","name",text));assert(!Json::GetString("{\"name\":\"unfinished","name",text));
 assert(!Json::GetInt("{\"count\":1.5}","count",count));assert(!Json::GetInt("{\"count\":99999999999999999}","count",count));
 std::cout<<"PASS: fail-closed allowlists, strict CIDR, Unicode JSON, nested/malformed input refusal and integer validation\n";
}
