/**
 * collectd - src/current_cost.c
 * Copyright (C) 2010  Manuel CISSÉ
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Manuel CISSÉ <manuel_cisse at yahoo.fr>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <termios.h>

#define MAX_SENSORS 12

typedef struct CurrentCostData {
  char            device[512];
  time_t          last_update[MAX_SENSORS];
  unsigned int    watts[MAX_SENSORS];
  int             device_fd;
  int             thread_loop;
  int             thread_error;
  pthread_t       thread_id;
  pthread_mutex_t lock;
} CurrentCostData;

CurrentCostData gdata;

static const char *config_keys[] =
  {
    "Device"
  };
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static void close_device(CurrentCostData *data)
{
  close(data->device_fd);
  data->device_fd = -1;
}

static int reopen_device(CurrentCostData *data)
{
  struct termios options;

  if(data->device_fd >= 0)
    close_device(data);

  data->device_fd = open(data->device, O_RDONLY | O_NOCTTY);
  if(data->device_fd < 0)
    {
      ERROR("current_cost plugin: failed to open device '%s': %s", data->device, strerror(errno));
      return -1;
    }
  tcgetattr(data->device_fd, &options);

  cfsetospeed(&options, B57600);
  options.c_cflag |= (CLOCAL | CREAD);

  options.c_cflag &= ~PARENB;
  options.c_cflag &= ~CSTOPB;
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;

  tcsetattr(data->device_fd, TCSANOW, &options);

  INFO("current_cost plugin: using device '%s'", data->device);

  return 0;
}

static void cc_process_measurement(CurrentCostData *data,
				   const char *line)
{
  char *ptr;
  unsigned int watts;
  unsigned int sensor;
  time_t cur_time;

  ptr = strstr(line, "<sensor>");
  if(ptr == NULL)
    return;
  ptr += 8;
  sensor = atoi(ptr);

  if(sensor >= MAX_SENSORS)
    return;

  ptr = strstr(line, "<watts>");
  if(ptr == NULL)
    return;
  ptr += 8;
  watts = atoi(ptr);

  cur_time = time(NULL);
  data->watts[sensor] = watts;
  data->last_update[sensor] = cur_time;
}

static void *cc_thread(CurrentCostData *data)
{
  char buff[32];
  char current_line[512];
  char *tmp, *pos, *end;
  unsigned int n;
  fd_set fdset;
  struct timeval tv;

  pos = current_line;
  end = current_line + sizeof(current_line) - 1;
  while(1)
    {
      FD_ZERO(&fdset);
      FD_SET(data->device_fd, &fdset);
      tv.tv_sec = 20;
      tv.tv_usec = 0;
      n = select(data->device_fd + 1, &fdset, NULL, NULL, &tv);
      if(n < 0)
	{
	  ERROR("current_cost plugin: device read error: %s", strerror(errno));
	  ERROR("current_cost plugin: reopening device");
	  if(reopen_device(data) == 0)
	      continue;
	  ERROR("current_cost plugin: reopen failed, aborting");
	  break;
	}
      else if(n == 0)
	{
	  ERROR("current_cost plugin: no data for 20s, reopening device");
	  if(reopen_device(data) == 0)
	    continue;
	  ERROR("current_cost plugin: reopen failed, aborting");
	  break;
	}
      else
	{
	  n = read(data->device_fd, buff, sizeof(buff));
	  if(n <= 0)
	    {
	      WARNING("current_cost plugin: read failed (%s), reopening device", strerror(errno));
	      if(reopen_device(data) == 0)
		continue;
	      ERROR("current_cost plugin: reopen failed, aborting");
	      break;
	    }
	  tmp = buff;
	  while(n > 0)
	    {
	      if(*tmp != '\n')
		{
		  *pos++ = *tmp++;
		  if(pos >= end)
		    pos = current_line;
		}
	      else
		{
		  *(pos - 1) = 0;
		  *pos = 0;
		  cc_process_measurement(data, current_line);
		  pos = current_line;
		}
	      n--;
	    }
	}

      pthread_mutex_lock(&data->lock);
      if(data->thread_loop <= 0)
	{
	  pthread_mutex_unlock(&data->lock);
	  break;
	}
      pthread_mutex_unlock(&data->lock);
    }

  return NULL;
}

static int start_thread(CurrentCostData *data)
{
  int status;

  pthread_mutex_lock(&data->lock);

  if(data->thread_loop != 0)
    {
      pthread_mutex_unlock(&data->lock);
      return -1;
    }

  data->thread_loop = 1;
  data->thread_error = 0;
  status = pthread_create(&data->thread_id, /* attr = */ NULL,
			  (void * (*)(void *)) cc_thread, data);
  if(status != 0)
    {
      data->thread_loop = 0;
      ERROR("current_cost plugin: Starting thread failed.");
      pthread_mutex_unlock(&data->lock);
      return -1;
    }

  pthread_mutex_unlock(&data->lock);
  return 0;
}

static int stop_thread(CurrentCostData *data)
{
  int status;

  pthread_mutex_lock(&data->lock);

  if(data->thread_loop == 0)
    {
      pthread_mutex_unlock(&data->lock);
      return -1;
    }

  data->thread_loop = 0;
  pthread_mutex_unlock(&data->lock);

  pthread_cancel(data->thread_id);
  status = pthread_join(data->thread_id, NULL);
  if(status != 0)
    {
      ERROR("current_cost plugin: Stopping thread failed.");
      status = -1;
    }

  return status;
}

static int cc_init(void)
{
  int i;

  gdata.device_fd = -1;
  for(i = 0; i < MAX_SENSORS; i++)
      gdata.last_update[i] = 0;
  gdata.thread_loop = 0;
  gdata.thread_error = 0;
  pthread_mutex_init(&gdata.lock, NULL);

  if(gdata.device[0] == 0)
    sstrncpy(gdata.device, "/dev/serial/by-id/usb-Prolific_Technology_Inc._USB-Serial_Controller-if00-port0", sizeof(gdata.device));

  if(reopen_device(&gdata) != 0)
    {
      ERROR("current_cost plugin: no device found");
      return -1;
    }

  if(start_thread(&gdata) != 0)
    {
      close_device(&gdata);
      return -1;
    }

  return 0;
}

static int cc_config(const char *key, const char *value)
{
  if(strcasecmp(key, "Device") == 0)
    sstrncpy(gdata.device, value, sizeof(gdata.device));

  return 0;
}

static void submit(int sensor, const char *type,
		   gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  char channel_s[8];

  ssnprintf(channel_s, sizeof(channel_s), "%d", sensor);

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "current_cost", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "", sizeof(vl.plugin_instance));
  sstrncpy(vl.type_instance, channel_s, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
}

static int cc_read(void)
{
  time_t min_time;
  CurrentCostData *data;
  int i;

  data = &gdata;

  min_time = time(NULL) - 90;
  pthread_mutex_lock(&data->lock);
  for(i = 0; i < MAX_SENSORS; i++)
    {
      if(data->last_update[i] > min_time)
	  submit(3 * i + 0, "power", data->watts[i]);
    }
  pthread_mutex_unlock(&data->lock);

  return 0;
}

static int cc_shutdown(void)
{
  INFO("current_cost plugin: Shutting down thread.");
  if(stop_thread(&gdata) < 0)
    {
      WARNING("current_cost plugin: Failed to stop thread.");
      return -1;
    }
  INFO("current_cost plugin: Thread stopped.");

  return 0;
}

void module_register(void)
{
  plugin_register_config("current_cost", cc_config,
			 config_keys, config_keys_num);
  plugin_register_init("current_cost", cc_init);
  plugin_register_read("current_cost", cc_read);
  plugin_register_shutdown("current_cost", cc_shutdown);
}
