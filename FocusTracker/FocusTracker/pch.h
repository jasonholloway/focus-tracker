#pragma once

#define NTDDI_VERSION 0x06010000
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <wtsapi32.h>
#include <thread>
#include <sstream>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/lockfree/policies.hpp>
#include <boost/asio.hpp>
