#ifndef SIMPLE_VM_NATIVE_FS_H
#define SIMPLE_VM_NATIVE_FS_H

#include <cstdio>
#include <string>
#include <vector>

namespace Simple::VM::Native::Fs {

bool ReadText(const std::string& path, std::string* out);
bool WriteText(const std::string& path, const std::string& text);
bool ReadBytes(const std::string& path, std::vector<int32_t>* out);
bool WriteBytes(const std::string& path, const std::vector<int32_t>& bytes);
bool ListDir(const std::string& path, std::vector<std::string>* out);
bool CopyFile(const std::string& from, const std::string& to);
bool Remove(const std::string& path);
bool Mkdir(const std::string& path);
bool MkdirAll(const std::string& path);
bool SetCwd(const std::string& path);
bool Cwd(std::string* out);
std::FILE* OpenFile(const std::string& path, const char* mode);

} // namespace Simple::VM::Native::Fs

#endif // SIMPLE_VM_NATIVE_FS_H
