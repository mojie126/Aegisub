/**
 * @file mimalloc_override.cpp
 * @brief 全局替换 C++ operator new/delete 为 mimalloc 实现
 *
 * 此文件必须且仅被编译一次，通过定义全局 operator new/delete
 * 将所有 C++ 动态分配重定向到 mimalloc 分配器。
 *
 * C 层面的 malloc/free/calloc/realloc 仍使用 CRT 默认实现，
 * 因为 /FImimalloc-override.h 强制包含方案与第三方源码存在宏冲突。
 */
#include <mimalloc-new-delete.h>
