#include <stdio.h>

#include <start.h>
#include <debug.h>
#include "file_monitor.h"

void client_start()
{
	_debug("client hahahaha\n");
	file_monitor_task();
	return;
}
