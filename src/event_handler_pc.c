/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/**********************************************************************
   Copyright [2014] [Cisco Systems, Inc.]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
**********************************************************************/

#include "parodus_log.h"
#include "event_handler_pc.h"
#include "connection.h"
#include "config.h"
#include "heartBeat.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "time.h"
#include "close_retry.h"

static pthread_t event_tid;

void *parodus_event_handler ()
{
	ParodusInfo("parodus_event_handler thread started\n");
	int val = 0;
	pthread_detach(pthread_self());

	while(1)
    	{
		val = InterfaceStatus();
		ParodusInfo("InterfaceStatus is %d\n", val);

		if (val!=0) 
		{
			ParodusInfo("Received wan-stop event, Close the connection\n");
			set_global_reconnect_reason("wan_down");
			set_global_reconnect_status(true);
			/* The wan-stop event should close the connection but shouldn't retry ,
			   inorder to avoid that interface_down_event flag is set 
			*/
		  	set_interface_down_event();
			ParodusInfo("Interface_down_event is set\n");				
			close_and_unref_connection (get_global_conn());
			set_global_conn(NULL);
			get_parodus_cfg()->cloud_status = CLOUD_STATUS_OFFLINE;
			ParodusPrint("cloud_status set as %s after interface_down_event\n", get_parodus_cfg()->cloud_status);
			pause_heartBeatTimer();

		}
		else
		{
			if(get_interface_down_event())
			{
			  	ParodusInfo("Received wan-started event, Close the connection and retry again \n");
				reset_interface_down_event();
				ParodusInfo("Interface_down_event is reset\n");
				resume_heartBeatTimer();
				set_close_retry();
			}
		}
		/* Perform interface status check after ping miss time interval 180s */
		ParodusInfo("check after 190s\n");
		sleep(190);
	}

    return 0;
}

void EventHandler()
{
	ParodusInfo("starting parodus_event_handler thread\n");
	pthread_create(&event_tid, NULL, parodus_event_handler, NULL);
	return;
}

int InterfaceStatus()
{
	int link[2];
	pid_t pid;
	char statusValue[512] = {'\0'};
	char* data =NULL;
	int status = -1;
	int nbytes =0;
	
	if (pipe(link) == -1)
	{
		ParodusInfo("Failed to create pipe for checking device interface\n");
		return status;
	}
	if ((pid = fork()) == -1)
	{
		ParodusInfo("fork was unsuccessful while checking device interface\n");
		return status;
	}
	
	if(pid == 0) 
	{
		ParodusInfo("child process created\n");
		printf("nbytes is %d\n", nbytes);
		pid = getpid();
		ParodusInfo("child process execution with pid:%d\n", pid);
		dup2 (link[1], STDOUT_FILENO);
		close(link[0]);
		close(link[1]);	
		execl("/sbin/ifconfig", "ifconfig", get_parodus_cfg()->webpa_interface_used, (char *)0);
	}	
	else 
	{
		close(link[1]);
		nbytes = read(link[0], statusValue, sizeof(statusValue));
		ParodusInfo("statusValue is :%s\n", statusValue);
		
		if ((data = strstr(statusValue, "inet addr:")) !=NULL)
		{
			ParodusInfo("Interface %s is up\n",get_parodus_cfg()->webpa_interface_used);
			status = 0;
		}
		else
		{
			ParodusInfo("Interface %s is down\n",get_parodus_cfg()->webpa_interface_used);
		}
		
		if(pid == wait(NULL))
		{
			ParodusInfo("child process pid %d terminated successfully\n", pid);
		}
		else
		{
			ParodusInfo("Error reading wait status of child process pid %d, killing it\n", pid);
			kill(pid, SIGKILL);
		}
		close(link[0]);
	}
	ParodusInfo("InterfaceStatus:%d\n", status);
	return status;
}

