#include <errno.h>
#include "bsrmon.h"
#include "module_debug.h"
#include "monitor_collect.h"

#ifdef _WIN
HANDLE
OpenDevice(PCHAR devicename)
{
	HANDLE		handle = INVALID_HANDLE_VALUE;

	handle = CreateFileA(devicename, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "LOG_ERROR: OpenDevice: cannot open %s\n", devicename);
	}

	return handle;
}

DWORD GetBsrDebugInfo(PBSR_DEBUG_INFO pDebugInfo)
{
	HANDLE      hDevice = INVALID_HANDLE_VALUE;
	DWORD       retVal = ERROR_SUCCESS;
	DWORD       dwReturned = 0;
	BOOL        ret = FALSE;

	hDevice = OpenDevice(MVOL_DEVICE);
	if (hDevice == INVALID_HANDLE_VALUE) {
		retVal = GetLastError();
		fprintf(stderr, "DEBUG_ERROR: %s: Failed open bsr. Err=%u\n",
			__FUNCTION__, retVal);
		return retVal;
	}
	ret = DeviceIoControl(hDevice, IOCTL_MVOL_GET_DEBUG_INFO,
		pDebugInfo, sizeof(BSR_DEBUG_INFO) + pDebugInfo->buf_size, pDebugInfo, sizeof(BSR_DEBUG_INFO) + pDebugInfo->buf_size, &dwReturned, NULL);
	if (ret == FALSE) {
		retVal = GetLastError();
	}

	printf("data %s\n", pDebugInfo->buf);

	if (hDevice != INVALID_HANDLE_VALUE)
		CloseHandle(hDevice);

	return retVal;
}

// BSR-37
enum BSR_DEBUG_FLAGS ConvertToBsrDebugFlags(char *str)
{
	if (!_strcmpi(str, "version")) return DBG_BSR_VERSION;
	else if (!_strcmpi(str, "in_flight_summary")) return DBG_RES_IN_FLIGHT_SUMMARY;
	else if (!_strcmpi(str, "state_twopc")) return DBG_RES_STATE_TWOPC;
	else if (!_strcmpi(str, "callback_history")) return DBG_CONN_CALLBACK_HISTORY;
	else if (!_strcmpi(str, "debug")) return DBG_CONN_DEBUG;
	else if (!_strcmpi(str, "conn_oldest_requests")) return DBG_CONN_OLDEST_REQUESTS;
	else if (!_strcmpi(str, "transport")) return DBG_CONN_TRANSPORT;
	else if (!_strcmpi(str, "transport_speed")) return DBG_CONN_TRANSPORT_SPEED;
	else if (!_strcmpi(str, "send_buf")) return DBG_CONN_SEND_BUF;
	else if (!_strcmpi(str, "proc_bsr")) return DBG_PEER_PROC_BSR;
	else if (!_strcmpi(str, "resync_extents")) return DBG_PEER_RESYNC_EXTENTS;
	else if (!_strcmpi(str, "act_log_extents")) return DBG_DEV_ACT_LOG_EXTENTS;
	else if (!_strcmpi(str, "act_log_stat")) return DBG_DEV_ACT_LOG_STAT;
	else if (!_strcmpi(str, "data_gen_id")) return DBG_DEV_DATA_GEN_ID;
	else if (!_strcmpi(str, "ed_gen_id")) return DBG_DEV_ED_GEN_ID;
	else if (!_strcmpi(str, "io_frozen")) return DBG_DEV_IO_FROZEN;
	else if (!_strcmpi(str, "dev_oldest_requests")) return DBG_DEV_OLDEST_REQUESTS;
	else if (!_strcmpi(str, "dev_io_stat")) return DBG_DEV_IO_STAT;
	else if (!_strcmpi(str, "dev_io_complete")) return DBG_DEV_IO_COMPLETE;
	else if (!_strcmpi(str, "dev_req_timing")) return DBG_DEV_REQ_TIMING;
	else if (!_strcmpi(str, "dev_peer_req_timing")) return DBG_DEV_PEER_REQ_TIMING;
	else if (!_strcmpi(str, "resync_ratio")) return DBG_CONN_RESYNC_RATIO;
	return DBG_NO_FLAGS;
}
#endif

/*
 * character removal 
*/
void eliminate(char *str, char ch)
{
	size_t len = strlen(str) + 1;
	for (; *str != '\0'; str++, len--) {
		if (*str == ch) {	
#ifdef _WIN	
			strcpy_s(str, len, str + 1);
#else
			strcpy(str, str + 1);
#endif
			str--;
		}
	}
}

void* exec_pipe(enum get_info_type info_type, char *res_name)
{
	char command[256];
	char buf[256] = { 0, };
	struct resource *res_head = NULL, *res = NULL, *res_temp = NULL;
	struct connection* conn = NULL, *conn_head = NULL, *conn_temp = NULL;
	struct volume *vol_head = NULL, *vol = NULL, *vol_temp = NULL;
	FILE *pipe;

	if (info_type == RESOURCE)
		sprintf_ex(command, "bsradm sh-resources-list");
	else if (info_type == CONNECTION)
		sprintf_ex(command, "bsradm sh-peer-nodes %s", res_name);
	else if (info_type == VOLUME)
		sprintf_ex(command, "bsradm sh-dev-vnr %s", res_name);
	else {
		fprintf(stderr, "Invalid get_info_type value\n");
		return NULL;
	}

#ifdef _WIN
	pipe = _popen(command, "r");
#else // _LIN
	pipe = popen(command, "r");
#endif
	if (!pipe) {
		fprintf(stderr, "Failed to execute command : %s\n", command);
		return NULL;
	}

	while (!feof(pipe)) {
		if (fgets(buf, 256, pipe) != NULL) {
			// remove EOL
			*(buf + (strlen(buf) - 1)) = 0;

			if (info_type == RESOURCE) {
				res = (struct resource*)malloc(sizeof(struct resource));
				if (!res) {
					fprintf(stderr, "Failed to malloc resource, size : %lu\n", sizeof(struct resource));
					return NULL;
				}
				res->conn = NULL;
				res->vol = NULL;
				eliminate(buf, '"');
#ifdef _WIN
				strcpy_s(res->name, buf);
#else // _LIN
				strcpy(res->name, buf);
#endif
				res->next = NULL;

				if (!res_temp)
					res_head = res;
				else
					res_temp->next = res;
				res_temp = res;
			}
			else if (info_type == CONNECTION) {
				conn = (struct connection*)malloc(sizeof(struct connection));
				if (!conn) {
					fprintf(stderr, "Failed to malloc connection, size : %lu\n", sizeof(struct connection));
					return NULL;
				}
				memset(conn, 0, sizeof(struct connection));
				
#ifdef _WIN
				sscanf_s(buf, "%d %s", &conn->node_id, conn->name, sizeof(conn->name));
#else // _LIN
				sscanf(buf, "%d %s", &conn->node_id, conn->name);
#endif

				conn->next = NULL;
				if (!conn_temp)
					conn_head = conn;
				else
					conn_temp->next = conn;
				conn_temp = conn;

			}
			else if (info_type == VOLUME) {
				vol = (struct volume*)malloc(sizeof(struct volume));
				if (!vol) {
					fprintf(stderr, "Failed to malloc volume, size : %lu\n", sizeof(struct volume));
					return NULL;
				}
				vol->vnr = atoi(buf);
				vol->next = NULL;

				if (!vol_temp)
					vol_head = vol;
				else
					vol_temp->next = vol;
				vol_temp = vol;
			}
		}
		else if (*buf == 0) {
			fprintf(stderr, "Failed to execute command : %s\n", command);
			return NULL;
		}
	}
#ifdef _WIN
	_pclose(pipe);
#else // _LIN
	pclose(pipe);
#endif

	if (info_type == RESOURCE) {
		return res_head;
	}
	else if (info_type == CONNECTION) {
		return conn_head;
	}
	else if (info_type == VOLUME) {
		return vol_head;
	}
	else {
		return NULL;
	}
}

void freeResource(struct resource* res)
{
	struct resource* res_temp;
	struct volume* vol_temp;
	struct connection* conn_temp;

	while (res) {
		while (res->conn) {
			conn_temp = res->conn;
			res->conn = res->conn->next;

			free(conn_temp);
			conn_temp = NULL;
		}

		while (res->vol) {
			vol_temp = res->vol;
			res->vol = res->vol->next;

			free(vol_temp);
			vol_temp = NULL;
		}

		res_temp = res;
		res = res->next;

		free(res_temp);
		res_temp = NULL;
	}
}

struct resource* GetResourceInfo()
{
	struct resource *res_head = NULL, *res = NULL;

	res = (struct resource*)exec_pipe(RESOURCE, NULL);
	res_head = res;
	while (res) {
		res->conn = (struct connection*)exec_pipe(CONNECTION, res->name);
		if (!res->conn) {
			freeResource(res);
			return NULL;
		}

		res->vol = (struct volume*)exec_pipe(VOLUME, res->name);
		if (!res->vol) {
			freeResource(res);
			return NULL;
		}

		res = res->next;
	}

	return res_head;
}

int CheckResourceInfo(char* resname, int node_id, int vnr)
{
	struct resource *res, *res_temp;
	struct volume *vol;
	struct connection *conn;
	int err = 0;

	res = GetResourceInfo();
	if (!res) {
		fprintf(stderr, "Failed in CheckResourceInfo(), not found resource %s\n", resname);
		return -1;
	}

	res_temp = res;
	while (res_temp) {
		// check resname
		if (!strcmp(resname, res_temp->name)) {
			// check node_id
			if (node_id != 0) {
				bool find_id = false;
				conn = res_temp->conn;
				while (conn) {
					if (node_id == conn->node_id) {
						find_id = true;
						break;
					} else
						conn = conn->next;
				}
				if (!find_id) {
					err = -1;
					fprintf(stderr, "Failed in CheckResourceInfo(), not found node-id:%d\n", node_id);
					goto ret;
				}
			}
			
			// check vnr
			if (vnr >= 0) {
				vol = res_temp->vol;
				while (vol) {
					if (vnr == vol->vnr)
						goto ret;
					else
						vol = vol->next;
				}
				fprintf(stderr, "Failed in CheckResourceInfo(), not found vnr:%d\n", vnr);
				err = -1;
			}
			
			goto ret;
		}
		else
			res_temp = res_temp->next;	
	}

	fprintf(stderr, "Failed in CheckResourceInfo(), not found resource %s\n", resname);
	err = -1;
ret:
	freeResource(res);
	return err;
}

#ifdef _WIN
PBSR_DEBUG_INFO GetDebugInfo(enum BSR_DEBUG_FLAGS flag, struct resource* res, int val)
{
	PBSR_DEBUG_INFO debugInfo;
	int size = MAX_DEBUG_BUF_SIZE;
	int ret = 0;

	debugInfo = (PBSR_DEBUG_INFO)malloc(sizeof(BSR_DEBUG_INFO) + size);
	if (!debugInfo) {
		fprintf(stderr, "DEBUG_ERROR: Failed to malloc BSR_DEBUG_INFO\n");
		return NULL;
	}
	memset(debugInfo, 0, sizeof(BSR_DEBUG_INFO) + size);
	debugInfo->peer_node_id = -1;
	debugInfo->vnr = -1;
	debugInfo->buf_size = size;
	debugInfo->flags = flag;

	strcpy_s(debugInfo->res_name, res->name);
	if (flag == DBG_DEV_IO_STAT || flag == DBG_DEV_IO_COMPLETE || 
			flag == DBG_DEV_REQ_TIMING || flag == DBG_DEV_PEER_REQ_TIMING || flag == DBG_DEV_ACT_LOG_STAT)
		debugInfo->vnr = val;
	else if (flag == DBG_CONN_TRANSPORT_SPEED || flag == DBG_CONN_SEND_BUF || flag == DBG_CONN_RESYNC_RATIO)
		debugInfo->peer_node_id = val;

	while ((ret = GetBsrDebugInfo(debugInfo)) != ERROR_SUCCESS) {
		if (ret == ERROR_MORE_DATA) {
			size <<= 1;

			if (size > MAX_DEBUG_BUF_SIZE << 10) { // 4M
				fprintf(stderr, "DEBUG_ERROR: Failed to get bsr debuginfo. (Err=%u)\n", ret);
				fprintf(stderr, "buffer overflow.\n");
				break;
			}

			// reallocate when buffer is insufficient
			debugInfo = (PBSR_DEBUG_INFO)realloc(debugInfo, sizeof(BSR_DEBUG_INFO) + size);
			if (!debugInfo) {
				fprintf(stderr, "DEBUG_ERROR: Failed to realloc BSR_DEBUG_INFO\n");
				break;
			}
			debugInfo->buf_size = size;
		}
		else {
			break;
		}
	}

	if (ret == ERROR_SUCCESS) {
		return debugInfo;
	}

	if (debugInfo) {
		free(debugInfo);
		debugInfo = NULL;
	}

	return NULL;
}
#endif

char* GetDebugToBuf(enum get_debug_type debug_type, struct resource *res) {
#ifdef _WIN
	PBSR_DEBUG_INFO debugInfo = NULL;
	enum BSR_DEBUG_FLAGS flag;
#else // _LIN
	char path[MAX_PATH];
	FILE *fp;
#endif
	char *buffer;

	if (!res) {
		fprintf(stderr, "Invalid res object\n");
		return NULL;
	}

	buffer = (char*)malloc(MAX_DEBUG_BUF_SIZE);
	if (!buffer) {
		fprintf(stderr, "Failed to malloc debug buffer\n");
		return NULL;
	}
	memset(buffer, 0, MAX_DEBUG_BUF_SIZE);

	if (debug_type <= REQUEST) {
		struct volume *vol = res->vol;
		if (!res->vol) {
			fprintf(stderr, "Invalid res->vol object\n");
			goto fail;
		}

		while (vol) {
#ifdef _WIN		
			if (debug_type == IO_STAT)
				flag = ConvertToBsrDebugFlags("dev_io_stat");
			else if (debug_type == IO_COMPLETE)
				flag = ConvertToBsrDebugFlags("dev_io_complete");
			else if (debug_type == REQUEST)
				flag = ConvertToBsrDebugFlags("dev_req_timing");
			else if (debug_type == PEER_REQUEST)
				flag = ConvertToBsrDebugFlags("dev_peer_req_timing");
			else if (debug_type == AL_STAT)
				flag = ConvertToBsrDebugFlags("act_log_stat");

			//sprintf_s(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), "vnr(%d):\n", vol->vnr);

			debugInfo = GetDebugInfo(flag, res, vol->vnr);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;
#else // _LIN
			if (debug_type == IO_STAT)
				sprintf(path, "%s/resources/%s/volumes/%d/io_stat", DEBUGFS_ROOT, res->name, vol->vnr);
			if (debug_type == IO_COMPLETE)
				sprintf(path, "%s/resources/%s/volumes/%d/io_complete", DEBUGFS_ROOT, res->name, vol->vnr);
			else if (debug_type == REQUEST) 
				sprintf(path, "%s/resources/%s/volumes/%d/req_timing", DEBUGFS_ROOT, res->name, vol->vnr);
			else if (debug_type == PEER_REQUEST) 
				sprintf(path, "%s/resources/%s/volumes/%d/peer_req_timing", DEBUGFS_ROOT, res->name, vol->vnr);
			else if (debug_type == AL_STAT) 
				sprintf(path, "%s/resources/%s/volumes/%d/act_log_stat", DEBUGFS_ROOT, res->name, vol->vnr);

			sprintf(buffer + strlen(buffer), "vnr(%d):\n", vol->vnr);
			
			fp = fopen(path, "r");
			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}

			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
#endif
			vol = vol->next;
		}
	}
	else if (debug_type == NETWORK_SPEED) {
		struct connection *conn = res->conn;
		if (!res->conn) {
			fprintf(stderr, "Invalid res->conn object\n");
			return NULL;
		}
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("transport_speed");

		while (conn) {
			sprintf_s(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), "%s:\n", conn->name);
			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;

			conn = conn->next;
		}
		
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/transport_speed", DEBUGFS_ROOT, res->name, conn->name);

			sprintf(buffer + strlen(buffer), "%s:\n", conn->name);
			fp = fopen(path, "r");
			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}

			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);

			conn = conn->next;
		}
		
#endif
	}
	else if (debug_type == SEND_BUF) {
		struct connection *conn = res->conn;
		if (!res->conn) {
			fprintf(stderr, "Invalid res->conn object\n");
			return NULL;
		}
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("send_buf");

		while (conn) {
			sprintf_s(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), "%s:\n", conn->name);
			
			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;
			conn = conn->next;
		}
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/send_buf", DEBUGFS_ROOT, res->name, conn->name);

			sprintf(buffer + strlen(buffer), "%s:\n", conn->name);	
			fp = fopen(path, "r");
			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}

			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
			conn = conn->next;
		}
#endif
	}
	// BSR-838
	else if (debug_type == RESYNC_RATIO) {
		struct connection *conn = res->conn;
		if (!res->conn) {
			fprintf(stderr, "Invalid res->conn object\n");
			return NULL;
		}
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("resync_ratio");

		while (conn) {
			sprintf_s(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), "%s:\n", conn->name);

			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;
			conn = conn->next;
		}
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/resync_ratio", DEBUGFS_ROOT, res->name, conn->name);

			sprintf(buffer + strlen(buffer), "%s:\n", conn->name);
			fp = fopen(path, "r");
			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}

			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
			conn = conn->next;
		}
#endif
	} else {
		fprintf(stderr, "Invalid debug_type value\n");
		goto fail;
	}

	return buffer;

fail:
	if (buffer) {
		free(buffer);
		buffer = NULL;
	}
	return NULL;
}

FILE *perf_fileopen(char * filename, char * currtime)
{
	FILE *fp;
	char new_filename[512];
	int err;
	off_t size;
	long file_rolling_size;

#ifdef _WIN
	fp = _fsopen(filename, "a", _SH_DENYNO);
#else // _LIN
	fp = fopen(filename, "a");
#endif
	if (fp == NULL) {
		fprintf(stderr, "Failed to open %s\n", filename);
		return NULL;
	}
	
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);

	file_rolling_size = GetOptionValue(FILE_ROLLING_SIZE);
	if (file_rolling_size <= 0)
		file_rolling_size = DEFAULT_FILE_ROLLING_SIZE;	

	if ((1024 * 1024 * file_rolling_size) < size) {
#ifdef _WIN
		HANDLE hFind;
		WIN32_FIND_DATA FindFileData;
		WCHAR dir_path[MAX_PATH] = { 0, };
		WCHAR find_file[MAX_PATH] = { 0, };
		char remove_file_path[MAX_PATH] = { 0, };
#else //_LIN
		DIR *dir_p = NULL;
		struct dirent* entry = NULL;
		char dir_path[MAX_PATH] = { 0, }; 
		char find_file[MAX_PATH] = { 0, };
#endif
		char remove_file[MAX_PATH+20] = { 0, };
		char r_time[64] = { 0, };
		char* ptr;
#ifdef _WIN
		std::set<std::wstring> listFileName;
		std::set<std::wstring>::reverse_iterator iter;
#else // _LIN
		std::set<std::string> listFileName;
		std::set<std::string>::reverse_iterator iter;
#endif
		int file_cnt = 0;
		int rolling_cnt = GetOptionValue(FILE_ROLLING_CNT);
		if (rolling_cnt <= 0)
			rolling_cnt = DEFAULT_FILE_ROLLONG_CNT;

		fclose(fp);

#ifdef _WIN
		wsprintf(dir_path, L"%S*", filename);
		ptr = strrchr(filename, '\\');
		memcpy(remove_file_path, filename, (ptr - filename));
		wsprintf(find_file, L"%S_", ptr + 1);
		hFind = FindFirstFile(dir_path, &FindFileData);
		if (hFind == INVALID_HANDLE_VALUE){
			fprintf(stderr, "Failed to open %s\n", dir_path);
			return NULL;
		}
		
		do{
			if (wcsstr(FindFileData.cFileName, find_file)) {
				listFileName.insert(FindFileData.cFileName);
			}
		} while (FindNextFile(hFind, &FindFileData));
		FindClose(hFind);

		for (iter = listFileName.rbegin(); iter != listFileName.rend(); iter++) {
			file_cnt++;
			if (file_cnt >= rolling_cnt) {
				sprintf_s(remove_file, "%s%s%ws", remove_file_path, _SEPARATOR_, iter->c_str());
				remove(remove_file);
			}
		}
#else // _LIN
		ptr = strrchr(filename, '/');
		memcpy(dir_path, filename, (ptr - filename));
		snprintf(find_file, strlen(ptr) + 1, "%s_", ptr + 1);

		if ((dir_p = opendir(dir_path)) == NULL) {
			fprintf(stderr, "Failed to open %s\n", dir_path);
			return NULL;
		}

		while ((entry = readdir(dir_p)) != NULL) {
			if (strstr(entry->d_name, find_file)) {
				listFileName.insert(entry->d_name);
			}
		}
		closedir(dir_p);

		for (iter = listFileName.rbegin(); iter != listFileName.rend(); iter++) {
			file_cnt++;
			if (file_cnt >= rolling_cnt) {
				sprintf_ex(remove_file, "%s%s%s", dir_path, _SEPARATOR_, iter->c_str());
				remove(remove_file);
			}
		}
#endif
		memcpy(r_time, currtime, strlen(currtime));
		eliminate(r_time, ':');
		printf("%s\n", r_time);
		sprintf_ex(new_filename, "%s_%s", filename, r_time);
		err = rename(filename, new_filename);
		if (err == -1) {
			fprintf(stderr, "Failed to log file rename %s => %s\n", filename, new_filename);
			return NULL;
		}
#ifdef _WIN
		fp = _fsopen(filename, "a", _SH_DENYNO);
#else // _LIN
		fp = fopen(filename, "a");
#endif
		if (fp == NULL) {
			fprintf(stderr, "Failed to open %s\n", filename);
			return NULL;
		}
	}

	
	return fp;

}

// BSR-740 before enable bsrmon_run, read the debugfs file once and initialize it.
int InitPerfType(enum get_debug_type debug_type, struct resource *res)
{
#ifdef _WIN
	PBSR_DEBUG_INFO debugInfo = NULL;
	enum BSR_DEBUG_FLAGS flag;
#else // _LIN
	char path[MAX_PATH];
	FILE *fp;
#endif	
	int ret = -1;
	char *buffer;

	buffer = (char*)malloc(MAX_DEBUG_BUF_SIZE);
	if (!buffer) {
		fprintf(stderr, "Failed to malloc debug buffer\n");
		return -1;
	}
	memset(buffer, 0, MAX_DEBUG_BUF_SIZE);

	if (debug_type <= REQUEST) {
		struct volume *vol = res->vol;
		if (!res->vol)
			goto fail;

		while (vol) {
#ifdef _WIN
			if (debug_type == IO_STAT)
				flag = ConvertToBsrDebugFlags("dev_io_stat");
			else if (debug_type == IO_COMPLETE)
				flag = ConvertToBsrDebugFlags("dev_io_complete");
			else if (debug_type == REQUEST)
				flag = ConvertToBsrDebugFlags("dev_req_timing");
			else if (debug_type == PEER_REQUEST)
				flag = ConvertToBsrDebugFlags("dev_peer_req_timing");
			else if (debug_type == AL_STAT)
				flag = ConvertToBsrDebugFlags("act_log_stat");
#else // _LIN 
			if (debug_type == IO_STAT)
				sprintf(path, "%s/resources/%s/volumes/%d/io_stat", DEBUGFS_ROOT, res->name, vol->vnr);
			else if (debug_type == IO_COMPLETE)
				sprintf(path, "%s/resources/%s/volumes/%d/io_complete", DEBUGFS_ROOT, res->name, vol->vnr);
			else if (debug_type == REQUEST)
				sprintf(path, "%s/resources/%s/volumes/%d/req_timing", DEBUGFS_ROOT, res->name, vol->vnr);
			else if (debug_type == PEER_REQUEST)
				sprintf(path, "%s/resources/%s/volumes/%d/peer_req_timing", DEBUGFS_ROOT, res->name, vol->vnr);
			else if (debug_type == AL_STAT)
				sprintf(path, "%s/resources/%s/volumes/%d/act_log_stat", DEBUGFS_ROOT, res->name, vol->vnr);
#endif

			

#ifdef _WIN
			debugInfo = GetDebugInfo(flag, res, vol->vnr);
			if (!debugInfo)
				goto fail;
			free(debugInfo);
			debugInfo = NULL;
#else // _LIN
			fp = fopen(path, "r");
			if (!fp) 
				goto fail;
			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
#endif
			vol = vol->next;
		}
	} else if (debug_type == NETWORK_SPEED) {
		struct connection *conn = res->conn;
		if (!res->conn) 
			return -1;
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("transport_speed");

		while (conn) {
			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo) 
				goto fail;

			free(debugInfo);
			debugInfo = NULL;
			conn = conn->next;
		}
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/transport_speed", DEBUGFS_ROOT, res->name, conn->name);

			fp = fopen(path, "r");
			if (!fp) {
				goto fail;
			}
			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
			conn = conn->next;
		}
#endif
	}
	// BSR-838 
	else if (debug_type == RESYNC_RATIO) {
		struct connection *conn = res->conn;
		if (!res->conn)
			return -1;
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("resync_ratio");

		while (conn) {
			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo)
				goto fail;

			free(debugInfo);
			debugInfo = NULL;
			conn = conn->next;
		}
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/resync_ratio", DEBUGFS_ROOT, res->name, conn->name);

			fp = fopen(path, "r");
			if (!fp) {
				goto fail;
			}
			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
			conn = conn->next;
		}
#endif
	} else {
		goto fail;
	}

	ret = 0;

fail:
	if (buffer) {
		free(buffer);
		buffer = NULL;
	}
	return ret;
}

// BSR-688 save aggregated debugfs data to file
int GetDebugToFile(enum get_debug_type debug_type, struct resource *res, char *respath, char * currtime)
{
#ifdef _WIN
	PBSR_DEBUG_INFO debugInfo = NULL;
	enum BSR_DEBUG_FLAGS flag;
#else // _LIN
	char path[MAX_PATH];
#endif

	FILE *fp;
	char outfile[MAX_PATH];
	
	char *buffer;
	int ret = -1;

	if (!res) {
		fprintf(stderr, "Invalid res object\n");
		return -1;
	}


	buffer = (char*)malloc(MAX_DEBUG_BUF_SIZE);
	if (!buffer) {
		fprintf(stderr, "Failed to malloc debug buffer\n");
		return -1;
	}
	memset(buffer, 0, MAX_DEBUG_BUF_SIZE);

	if (debug_type <= REQUEST) {
		struct volume *vol = res->vol;
		if (!res->vol) {
			fprintf(stderr, "Invalid res->vol object\n");
			goto fail;
		}

		while (vol) {

			if (debug_type == IO_STAT) {
#ifdef _WIN
				flag = ConvertToBsrDebugFlags("dev_io_stat");
#else // _LIN
				sprintf(path, "%s/resources/%s/volumes/%d/io_stat", DEBUGFS_ROOT, res->name, vol->vnr);
#endif
				sprintf_ex(outfile, "%s%svnr%d_IO_STAT", respath, _SEPARATOR_, vol->vnr);
			} else if (debug_type == IO_COMPLETE) {
#ifdef _WIN
				flag = ConvertToBsrDebugFlags("dev_io_complete");
#else // _LIN
				sprintf(path, "%s/resources/%s/volumes/%d/io_complete", DEBUGFS_ROOT, res->name, vol->vnr);
#endif
				sprintf_ex(outfile, "%s%svnr%d_IO_COMPLETE", respath, _SEPARATOR_, vol->vnr);
			} else if (debug_type == REQUEST) {
#ifdef _WIN
				flag = ConvertToBsrDebugFlags("dev_req_timing");
#else // _LIN
				sprintf(path, "%s/resources/%s/volumes/%d/req_timing", DEBUGFS_ROOT, res->name, vol->vnr);
#endif
				sprintf_ex(outfile, "%s%svnr%d_request", respath, _SEPARATOR_, vol->vnr);
			} else if (debug_type == PEER_REQUEST) {
#ifdef _WIN
				flag = ConvertToBsrDebugFlags("dev_peer_req_timing");
#else // _LIN
				sprintf(path, "%s/resources/%s/volumes/%d/peer_req_timing", DEBUGFS_ROOT, res->name, vol->vnr);
#endif
				sprintf_ex(outfile, "%s%svnr%d_peer_request", respath, _SEPARATOR_, vol->vnr);
			} else if (debug_type == AL_STAT) {
#ifdef _WIN
				flag = ConvertToBsrDebugFlags("act_log_stat");
#else // _LIN
				sprintf(path, "%s/resources/%s/volumes/%d/act_log_stat", DEBUGFS_ROOT, res->name, vol->vnr);
#endif
				sprintf_ex(outfile, "%s%svnr%d_al_stat", respath, _SEPARATOR_, vol->vnr);
			}


#ifdef _WIN
			debugInfo = GetDebugInfo(flag, res, vol->vnr);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;
#else // _LIN
			fp = fopen(path, "r");

			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}
			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			
			ret = errno;

			fclose(fp);

			if (ret == ENODEV)
				goto fail;
#endif
			// BSR-776 do not write error messages to the performance file.
			if (!strncmp(buffer, "err reading", 11))
				goto fail;

			fp = perf_fileopen(outfile, currtime);
			if (fp == NULL) 
				goto fail;

			fprintf(fp, "%s %s", currtime, buffer);	
			fclose(fp);
			
			memset(buffer, 0, MAX_DEBUG_BUF_SIZE);

			vol = vol->next;
		}
	}
	else if (debug_type == NETWORK_SPEED) {
		struct connection *conn = res->conn;
		if (!res->conn) {
			fprintf(stderr, "Invalid res->conn object\n");
			return -1;
		}
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("transport_speed");

		while (conn) {
			sprintf_s(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), "%s ", conn->name);
			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;
			conn = conn->next;
		}
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/transport_speed", DEBUGFS_ROOT, res->name, conn->name);

			sprintf(buffer + strlen(buffer), "%s ", conn->name);
			fp = fopen(path, "r");
			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}

			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
			conn = conn->next;
		}
#endif
		// BSR-776 do not write error messages to the performance file.
		if (!strncmp(buffer, "err reading", 11))
			goto fail;

		sprintf_ex(outfile, "%s%snetwork", respath, _SEPARATOR_);
		
		fp = perf_fileopen(outfile, currtime);
		if (fp == NULL)
			goto fail;

		fprintf(fp, "%s %s\n", currtime, buffer);	
		fclose(fp);

	}
	else if (debug_type == SEND_BUF) {
		struct connection *conn = res->conn;
		if (!res->conn) {
			fprintf(stderr, "Invalid res->conn object\n");
			return -1;
		}
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("send_buf");
		
		while (conn) {
			sprintf_s(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), "%s ", conn->name);
			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;
			conn = conn->next;

		}
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/send_buf", DEBUGFS_ROOT, res->name, conn->name);

			sprintf(buffer + strlen(buffer), "%s ", conn->name);
			fp = fopen(path, "r");
			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}

			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
			conn = conn->next;
		}
#endif
		// BSR-776 do not write error messages to the performance file.
		if (!strncmp(buffer, "err reading", 11))
			goto fail;
		sprintf_ex(outfile, "%s%ssend_buffer", respath, _SEPARATOR_);

		fp = perf_fileopen(outfile, currtime);
		if (fp == NULL)
			goto fail;

		fprintf(fp, "%s %s\n", currtime, buffer);
		fclose(fp);
	}
	// BSR-838 
	else if (debug_type == RESYNC_RATIO) {
		struct connection *conn = res->conn;
		if (!res->conn) {
			fprintf(stderr, "Invalid res->conn object\n");
			return -1;
		}
#ifdef _WIN
		flag = ConvertToBsrDebugFlags("resync_ratio");

		while (conn) {
			sprintf_s(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), "%s ", conn->name);
			debugInfo = GetDebugInfo(flag, res, conn->node_id);
			if (!debugInfo) {
				fprintf(stderr, "Failed to get bsr debuginfo(%d).\n", flag);
				goto fail;
			}

			memcpy(buffer + strlen(buffer), debugInfo->buf, strlen(debugInfo->buf));
			free(debugInfo);
			debugInfo = NULL;
			conn = conn->next;

		}
#else // _LIN
		while (conn) {
			sprintf(path, "%s/resources/%s/connections/%s/resync_ratio", DEBUGFS_ROOT, res->name, conn->name);

			sprintf(buffer + strlen(buffer), "%s ", conn->name);
			fp = fopen(path, "r");
			if (!fp) {
				fprintf(stderr, "Failed to open file, path : %s\n", path);
				goto fail;
			}

			fread(buffer + strlen(buffer), MAX_DEBUG_BUF_SIZE - strlen(buffer), 1, fp);
			fclose(fp);
			conn = conn->next;
		}
#endif
		// BSR-776 do not write error messages to the performance file.
		if (!strncmp(buffer, "err reading", 11))
			goto fail;
		sprintf_ex(outfile, "%s%sresync_ratio", respath, _SEPARATOR_);
		printf("outfile %s\n", outfile);

		fp = perf_fileopen(outfile, currtime);
		if (fp == NULL)
			goto fail;

		fprintf(fp, "%s %s\n", currtime, buffer);
		fclose(fp);
	} else {
		fprintf(stderr, "Invalid debug_type value\n");
		goto fail;
	}

	ret = 0;

fail:

	if (buffer) {
		free(buffer);
		buffer = NULL;
	}
	return ret;
}

// BSR-688 save memory info to file
int GetMemInfoToFile(char *path, char * currtime)
{
	FILE *fp;
	char outfile[MAX_PATH] = {0,};
	char *buffer = NULL;
	int ret = -1;


	sprintf_ex(outfile, "%s%smemory", path, _SEPARATOR_);

	fp = perf_fileopen(outfile, currtime);
	if (fp == NULL)
		goto fail;

	fprintf(fp, "%s ", currtime);

	buffer = GetBsrMemoryUsage();
	if (buffer) {
		fprintf(fp, "%s", buffer);
		free(buffer);
		buffer = NULL;
	}

	buffer = GetBsrUserMemoryUsage();
	if (buffer) {
		fprintf(fp, "%s", buffer);
		free(buffer);
		buffer = NULL;
	}
	fprintf(fp, "\n");
	fclose(fp);

	ret = 0;

fail:
	if (buffer) {
		free(buffer);
		buffer = NULL;
	}
	return ret;
}