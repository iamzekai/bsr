#ifdef _WIN
#include <tchar.h>
#else // _LIN
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../../../bsr-headers/bsr_ioctl.h"
#endif
#include <time.h>
#include <sys/timeb.h>
#include "bsrmon.h"
#include "module_debug.h"
#include "monitor_collect.h"
#include "read_stat.h"


#ifdef _WIN
void debug_usage()
{
	printf("usage: bsrmon /debug cmds options \n\n"
		"cmds:\n");
	printf(
		"   version\n"
		"   in_flight_summary {resource}\n"
		"   state_twopc {resource}\n"
		"   callback_history {resource} {peer_node_id}\n"
		"   debug {resource} {peer_node_id}\n"
		"   conn_oldest_requests {resource} {peer_node_id}\n"
		"   transport {resource} {peer_node_id}\n"
		"   transport_speed {resource} {peer_node_id}\n"
		"   send_buf {resource} {peer_node_id}\n"
		"   proc_bsr {resource} {peer_node_id} {volume}\n"
		"   resync_extents {resource} {peer_node_id} {volume}\n"
		"   act_log_extents {resource} {volume}\n"
		"   act_log_stat {resource} {volume}\n"
		"   data_gen_id {resource} {volume}\n"
		"   ed_gen_id {resource} {volume}\n"
		"   io_frozen {resource} {volume}\n"
		"   dev_oldest_requests {resource} {volume}\n"
		"   dev_io_stat {resource} {volume}\n"
		"   dev_io_complete {resource} {volume}\n"
		"   dev_req_timing {resource} {volume}\n"
		"   dev_peer_req_timing {resource} {volume}\n"
		"	dev_peer_ratio {resource} {peer_node_id}\n"
		);
	printf("\n");

	printf(
		"\n\n"
		"examples:\n"
		"bsrmon /debug version \n"
		"bsrmon /debug in_flight_summary r1 \n"
		"bsrmon /debug transport r1 1\n"
		"bsrmon /debug proc_bsr r1 1 0\n"
		"bsrmon /debug io_frozen r1 0\n"
		"bsrmon /debug resync_ratio r1\n"
		);

	exit(ERROR_INVALID_PARAMETER);
}
#endif

void perf_test_usage()
{
	printf("I/O performance degradation simulation. Absolutely only for testing purposes!\n"
		"usage: bsrmon /io_delay_test {flag} {delay point} {delay time} \n\n"
		"flag:\n"
		"   0: disable\n"
		"   1: enable\n"
		"\n\n"

		"delay point:\n"
		"   0: write I/O occurrence\n"
		"   1: master I/O completion\n"
		"   2: active log commit\n"
		"   3: submit\n"
		"   4: socket send\n"
		"   5: socket receive\n"
		"   6: peer request submit\n"
		"\n\n"

		"delay time:\n"
		"   0 ~ 500 ms\n"

		);
	exit(ERROR_INVALID_PARAMETER);
}

void usage()
{
	printf("usage: bsrmon cmds options \n\n"
		"cmds:\n");

	printf(
		"   /start\n"
		"   /stop\n"
		"   /status\n"
		//"   /print\n"
		//"   /file\n"
		"   /watch {types} [/scroll]\n"
		"   /report {types} [/f {filename}] [/d {YYYY-MM-DD}] [/s {hh:mm[:ss]}] [/e {hh:mm[:ss]}]\n"
		"   /set {period, file_size, file_cnt} {value}\n"
		"   /get {all, period, file_size, file_cnt}\n"
		"   /io_delay_test {flag} {delay point} {delay time}\n"
		);
#ifdef _WIN
	printf(
		"   /debug cmds options\n"
		);
#endif
	printf(
		"\n\n"
		"Types:\n"
		"   iostat {resource} {vnr}\n"
		"   ioclat {resource} {vnr}\n"
		"   reqstat {resource} {vnr}\n"
		"   peer_reqstat {resource} {vnr}\n"
		"   alstat {resource} {vnr}\n"
		"   network {resource}\n"
		"   sendbuf {resource}\n"
		"   resync_ratio {resource}\n"
		"   memstat \n"
		);
	exit(ERROR_INVALID_PARAMETER);
}

#ifdef _WIN
// BSR-37 debugfs porting
int BsrDebug(int argc, char* argv[])
{
	DWORD ret = ERROR_SUCCESS;
	int argIndex = 0;
	PBSR_DEBUG_INFO debugInfo = NULL;
	int size = MAX_SEQ_BUF;
	enum BSR_DEBUG_FLAGS flag;

	flag = ConvertToBsrDebugFlags(argv[argIndex]);

	if (!flag)
		debug_usage();

	if (flag == DBG_DEV_ACT_LOG_EXTENTS)
		size <<= 10;  // 4M

	debugInfo = (PBSR_DEBUG_INFO)malloc(sizeof(BSR_DEBUG_INFO) + size);
	if (!debugInfo) {
		fprintf(stderr, "DEBUG_ERROR: Failed to malloc BSR_DEBUG_INFO\n");
		return  ERROR_NOT_ENOUGH_MEMORY;
	}

	memset(debugInfo, 0, sizeof(BSR_DEBUG_INFO) + size);
	debugInfo->peer_node_id = -1;
	debugInfo->vnr = -1;
	debugInfo->buf_size = size;
	debugInfo->flags = flag;

	if (debugInfo->flags != DBG_BSR_VERSION) {
		argIndex++;
		if (argIndex < argc)
			strcpy_s(debugInfo->res_name, argv[argIndex]);
		else
			debug_usage();
		argIndex++;
		switch (debugInfo->flags) {
		case DBG_RES_IN_FLIGHT_SUMMARY:
		case DBG_RES_STATE_TWOPC:
			break;
		case DBG_CONN_CALLBACK_HISTORY:
		case DBG_CONN_DEBUG:
		case DBG_CONN_OLDEST_REQUESTS:
		case DBG_CONN_TRANSPORT:
		case DBG_CONN_TRANSPORT_SPEED:
		case DBG_CONN_SEND_BUF:
		case DBG_CONN_RESYNC_RATIO:
			if (argIndex < argc)
				debugInfo->peer_node_id = atoi(argv[argIndex]);
			else
				debug_usage();
			break;
		case DBG_PEER_PROC_BSR:
		case DBG_PEER_RESYNC_EXTENTS:
			if (argIndex < argc)
				debugInfo->peer_node_id = atoi(argv[argIndex]);
			else
				debug_usage();
			argIndex++;
			if (argIndex < argc)
				debugInfo->vnr = atoi(argv[argIndex]);
			else
				debug_usage();
			break;
		case DBG_DEV_ACT_LOG_EXTENTS:
		case DBG_DEV_ACT_LOG_STAT:
		case DBG_DEV_DATA_GEN_ID:
		case DBG_DEV_ED_GEN_ID:
		case DBG_DEV_IO_FROZEN:
		case DBG_DEV_OLDEST_REQUESTS:
		case DBG_DEV_IO_STAT:
		case DBG_DEV_IO_COMPLETE:
		case DBG_DEV_REQ_TIMING:
		case DBG_DEV_PEER_REQ_TIMING:
			if (argIndex < argc)
				debugInfo->vnr = atoi(argv[argIndex]);
			else
				debug_usage();
			break;
		default:
			break;
		}
	}


	while ((ret = GetBsrDebugInfo(debugInfo)) != ERROR_SUCCESS) {
		if (ret == ERROR_MORE_DATA) {
			size <<= 1;

			if (size > MAX_SEQ_BUF << 10) { // 4M
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
			fprintf(stderr, "DEBUG_ERROR: Failed to get bsr debuginfo. (Err=%u)\n", ret);
			break;
		}
	}

	if (ret == ERROR_SUCCESS) {
		fprintf(stdout, "%s\n", debugInfo->buf);
	}
	else if (ret == ERROR_INVALID_PARAMETER) {
		fprintf(stderr, "invalid paramter.\n");
	}

	if (debugInfo) {
		free(debugInfo);
		debugInfo = NULL;
	}

	return ret;
}
#endif

void PrintMonitor()
{	
	char *buf = NULL;
	struct resource* res;
	
	res = GetResourceInfo();
	if (!res) {
		fprintf(stderr, "Failed to get resource info.\n");
		return;
	}

	// print I/O monitoring status
	printf("IO_STAT:\n");
	buf = GetDebugToBuf(IO_STAT, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	printf("IO_COMPLETE:\n");
	buf = GetDebugToBuf(IO_COMPLETE, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	printf("REQUEST:\n");
	buf = GetDebugToBuf(REQUEST, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	printf("PEER_REQUEST:\n");
	buf = GetDebugToBuf(PEER_REQUEST, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	printf("AL_STAT:\n");
	buf = GetDebugToBuf(AL_STAT, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	// BSR-838
	printf("RESYNC_RATIO:\n");
	buf = GetDebugToBuf(RESYNC_RATIO, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	// print memory monitoring status
	printf("Memory:\n");
	buf = GetBsrMemoryUsage();
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}
	buf = GetBsrUserMemoryUsage();
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	// print network monitoring status
	printf("Network:\n");
	buf = GetDebugToBuf(NETWORK_SPEED, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}
	buf = GetDebugToBuf(SEND_BUF, res);
	if (buf) {
		printf("%s\n", buf);
		free(buf);
		buf = NULL;
	}

	freeResource(res);
}

// BSR-740 init performance data
void InitMonitor()
{
	struct resource* res;

	res = GetResourceInfo();
	if (!res) {
		fprintf(stderr, "Failed to get resource info.\n");
		return;
	}

	while (res) {
		if (InitPerfType(IO_STAT, res) != 0)
			goto next;
		if (InitPerfType(IO_COMPLETE, res) != 0)
			goto next;
		if (InitPerfType(REQUEST, res) != 0)
			goto next;
		if (InitPerfType(PEER_REQUEST, res) != 0)
			goto next;
		if (InitPerfType(AL_STAT, res) != 0)
			goto next;
		if (InitPerfType(NETWORK_SPEED, res) != 0)
			goto next;
		if (InitPerfType(RESYNC_RATIO, res) != 0)
			goto next;
next:
		res = res->next;
	}

	freeResource(res);
}


// BSR-688 save aggregated data to file
void MonitorToFile()
{
#ifdef _WIN
	size_t path_size;
	errno_t result;
	char bsr_path[MAX_PATH] = {0,};
#endif
	char perfpath[MAX_PATH] = {0,};
	struct resource* res;
	struct tm base_date_local;
	struct timeb timer_msec;
	char curr_time[64] = {0,};
	
	res = GetResourceInfo();
	if (!res) {
		fprintf(stderr, "Failed to get resource info.\n");
		return;
	}

#ifdef _WIN
	result = getenv_s(&path_size, bsr_path, MAX_PATH, "BSR_PATH");
	if (result) {
		strcpy_s(bsr_path, "c:\\Program Files\\bsr\\bin");
	}
	strncpy_s(perfpath, bsr_path, strlen(bsr_path) - strlen("bin"));
	strcat_s(perfpath, "log\\perfmon\\");

	ftime(&timer_msec);
	localtime_s(&base_date_local, &timer_msec.time);
#else
	sprintf(perfpath, "/var/log/bsr/perfmon/");

	ftime(&timer_msec);
	base_date_local = *localtime(&timer_msec.time);
#endif	
	sprintf_ex(curr_time, "%04d-%02d-%02d_%02d:%02d:%02d.%03d",
		base_date_local.tm_year + 1900, base_date_local.tm_mon + 1, base_date_local.tm_mday,
		base_date_local.tm_hour, base_date_local.tm_min, base_date_local.tm_sec, timer_msec.millitm);


	while (res) {
		char respath[MAX_PATH+RESOURCE_NAME_MAX] = {0,};

		sprintf_ex(respath, "%s%s", perfpath, res->name);
#ifdef _WIN
		CreateDirectoryA(respath, NULL);
#else // _LIN
		
		mkdir(respath, 0777);
#endif

		// save monitoring status
		if (GetDebugToFile(IO_STAT, res, respath, curr_time) != 0)
			goto next;
		if (GetDebugToFile(IO_COMPLETE, res, respath, curr_time) != 0)
			goto next;
		if (GetDebugToFile(REQUEST, res, respath, curr_time) != 0)
			goto next;
		if (GetDebugToFile(PEER_REQUEST, res, respath, curr_time) != 0)
			goto next;
		if (GetDebugToFile(AL_STAT, res, respath, curr_time) != 0)
			goto next;
		if (GetDebugToFile(NETWORK_SPEED, res, respath, curr_time) != 0)
			goto next;
		if (GetDebugToFile(SEND_BUF, res, respath, curr_time) != 0)
			goto next;
		if (GetDebugToFile(RESYNC_RATIO, res, respath, curr_time) != 0)
			goto next;
next:
		res = res->next;
	}

	// save memory monitoring status
	GetMemInfoToFile(perfpath, curr_time);

	freeResource(res);
}

// BSR-741 checking the performance monitor status
static bool is_running()
{
#ifdef _WIN
	HANDLE      hDevice = INVALID_HANDLE_VALUE;
	DWORD       dwReturned = 0;
#else // _LIN
	int fd = 0;
#endif
	unsigned int run = 0;
	int err = 0;

#ifdef _WIN
	hDevice = OpenDevice(MVOL_DEVICE);
	if (hDevice == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Failed to open bsr\n");
		return false;
	}
	
	if (DeviceIoControl(hDevice, IOCTL_MVOL_GET_BSRMON_RUN, NULL, 0, &run, sizeof(unsigned int), &dwReturned, NULL) == FALSE)
		err = 1;
	
	if (hDevice != INVALID_HANDLE_VALUE)
		CloseHandle(hDevice);

#else // _LIN
	if ((fd = open(BSR_CONTROL_DEV, O_RDWR)) == -1) {
		fprintf(stderr, "Can not open /dev/bsr-control\n");
		return false;
	}
	if (ioctl(fd, IOCTL_MVOL_GET_BSRMON_RUN, &run) != 0)
		err = 1;

	if (fd)
		close(fd);

#endif

	if (err){
		fprintf(stderr, "Failed to IOCTL_MVOL_GET_BSRMON_RUN\n");
		return false;
	} else if (run) {
		fprintf(stdout, "bsr performance monitor is enabled.\n");
		return true;
	} else {
		fprintf(stdout, "bsr performance monitor is disabled.\n");
		return false;
	}

}

// BSR-688 watching perf file
void Watch(char *resname, int type, int vnr, bool scroll)
{
	char watch_path[512] = {0,};
	
	char perf_path[MAX_PATH] = {0,};
#ifdef _WIN
	char bsr_path[MAX_PATH] = {0,};
	size_t path_size;
	errno_t result;
	result = getenv_s(&path_size, bsr_path, MAX_PATH, "BSR_PATH");
	if (result) {
		strcpy_s(bsr_path, "c:\\Program Files\\bsr\\bin");
	}
	strncpy_s(perf_path, bsr_path, strlen(bsr_path) - strlen("bin"));
	strcat_s(perf_path, "log\\perfmon\\");
#else // _LIN
	sprintf(perf_path, "/var/log/bsr/perfmon/");
#endif

	if (!is_running())
		return;

	if (resname != NULL) {
		int err = CheckResourceInfo(resname, 0, vnr);
		if (err) 
			return;
		
	}

	clear_screen();

	if (type != -1) {
		switch (type) {
		case IO_STAT:
			sprintf_ex(watch_path, "%s%s%svnr%d_IO_STAT", perf_path, resname, _SEPARATOR_, vnr);
			watch_io_stat(watch_path, scroll);
			break;
		case IO_COMPLETE:
			sprintf_ex(watch_path, "%s%s%svnr%d_IO_COMPLETE", perf_path, resname, _SEPARATOR_,vnr);
			watch_io_complete(watch_path, scroll);
			break;
		case REQUEST:
			sprintf_ex(watch_path, "%s%s%svnr%d_request", perf_path, resname, _SEPARATOR_,vnr);
			watch_req_stat(watch_path, scroll);
			break;
		case PEER_REQUEST:
			sprintf_ex(watch_path, "%s%s%svnr%d_peer_request", perf_path, resname, _SEPARATOR_,vnr);
			watch_peer_req_stat(watch_path, scroll);
			break;
		case AL_STAT:
			sprintf_ex(watch_path, "%s%s%svnr%d_al_stat", perf_path, resname, _SEPARATOR_,vnr);
			watch_al_stat(watch_path, scroll);
			break;
		case NETWORK_SPEED:
			sprintf_ex(watch_path, "%s%s%snetwork", perf_path, resname, _SEPARATOR_);
			watch_network_speed(watch_path, scroll);
			break;
		case SEND_BUF:
			sprintf_ex(watch_path, "%s%s%ssend_buffer", perf_path, resname, _SEPARATOR_);
			watch_sendbuf(watch_path, scroll);
			break;
		case MEMORY:
			sprintf_ex(watch_path, "%smemory", perf_path);
			watch_memory(watch_path, scroll);
			break;
		case RESYNC_RATIO:
			sprintf_ex(watch_path, "%s%s%sresync_ratio", perf_path, resname, _SEPARATOR_);
			watch_peer_resync_ratio(watch_path, scroll);
			break;

		default:
			usage();
		}
	}
	else {
		// TODO watch all
		//watch_all_type = true;
	}

}

void Report(char *resname, char *file, int type, int vnr, struct time_filter *tf)
{
	char filepath[512] = {0,};
	char perf_path[MAX_PATH] = {0,};
#ifdef _WIN
	char bsr_path[MAX_PATH] = {0,};
	size_t path_size;
	errno_t result;
	result = getenv_s(&path_size, bsr_path, MAX_PATH, "BSR_PATH");
	if (result) {
		strcpy_s(bsr_path, "c:\\Program Files\\bsr\\bin");
	}
	strncpy_s(perf_path, bsr_path, strlen(bsr_path) - strlen("bin"));
	strcat_s(perf_path, "log\\perfmon");
#else
	sprintf(perf_path, "/var/log/bsr/perfmon");
#endif

	if (resname)
		printf("Report %s ", resname);
	switch (type) {
	case IO_STAT:
		if (!file) {
			printf("[IO STAT - vnr%u]\n", vnr);
			sprintf_ex(filepath, "%s%s%s%svnr%d_IO_STAT", perf_path, _SEPARATOR_, resname, _SEPARATOR_, vnr);
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("[%s]\n", filepath);
		}
		
		read_io_stat_work(filepath, tf);
		break;
	case IO_COMPLETE:
		if (!file) {
			printf("[IO COMPLETE - vnr%u]\n", vnr);
			sprintf_ex(filepath, "%s%s%s%svnr%d_IO_COMPLETE", perf_path, _SEPARATOR_, resname, _SEPARATOR_, vnr);
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("[%s]\n", filepath);
		}
		read_io_complete_work(filepath, tf);
		break;
	case REQUEST:
		if (!file) {
			printf("[REQUEST STAT - vnr%u]\n", vnr);
			sprintf_ex(filepath, "%s%s%s%svnr%d_request", perf_path, _SEPARATOR_, resname, _SEPARATOR_, vnr);
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("[%s]\n", filepath);
		}
		read_req_stat_work(filepath, resname, tf);
		break;
	case PEER_REQUEST:
		if (!file) {
			printf("[PEER REQUEST STAT - vnr%u]\n", vnr);
			sprintf_ex(filepath, "%s%s%s%svnr%d_peer_request", perf_path, _SEPARATOR_, resname, _SEPARATOR_, vnr);
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("[%s]\n", filepath);
		}
		read_peer_stat_work(filepath, resname, PEER_REQUEST, tf);
		
		break;
	case AL_STAT:
		if (!file) {
			printf("[AL STAT - vnr%u]\n", vnr);
			sprintf_ex(filepath, "%s%s%s%svnr%d_al_stat", perf_path, _SEPARATOR_, resname, _SEPARATOR_, vnr);
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("[%s]\n", filepath);
		}
		read_al_stat_work(filepath, tf);
		break;
	case NETWORK_SPEED:
		if (!file) {
			printf("[NETWORK SPEED]\n");
			sprintf_ex(filepath, "%s%s%s%snetwork", perf_path, _SEPARATOR_, resname, _SEPARATOR_);
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("[%s]\n", filepath);
		}
		read_peer_stat_work(filepath, resname, NETWORK_SPEED, tf);

		break;
	case SEND_BUF:
		if (!file) {
			printf("[SEND BUFFER]\n");
			sprintf_ex(filepath, "%s%s%s%ssend_buffer", perf_path, _SEPARATOR_, resname, _SEPARATOR_);
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("[%s]\n", filepath);
		}
		read_peer_stat_work(filepath, resname, SEND_BUF, tf);
		break;
	case MEMORY:
		if (!file) {
			sprintf_ex(filepath, "%s%smemory", perf_path, _SEPARATOR_);
			printf("Report [MEMORY]\n");
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("Report [%s]\n", filepath);
		}
		
		read_memory_work(filepath, tf);
		break;
	case RESYNC_RATIO:
		if (!file) {
			sprintf_ex(filepath, "%s%s%s%sresync_ratio", perf_path, _SEPARATOR_, resname, _SEPARATOR_);
			printf("Report [RESYNC_RATIO]\n");
		} else {
			sprintf_ex(filepath, "%s", file);
			printf("Report [%s]\n", filepath);
		}

		read_peer_stat_work(filepath, resname, RESYNC_RATIO, tf);
		break;
	default:
		usage();
	}
	
}


// BSR-694
void SetOptionValue(enum set_option_type option_type, long value)
{
#ifdef _WIN
	HKEY hKey = NULL;
	const TCHAR bsrRegistry[] = _T("SYSTEM\\CurrentControlSet\\Services\\bsr");
	DWORD type = REG_DWORD;
	DWORD size = sizeof(DWORD);
	DWORD lResult = ERROR_SUCCESS;
	DWORD option_value = value;
#else // _LIN
	FILE *fp;
#endif


	if (value <= 0) {
		fprintf(stderr, "Failed to set option value %ld\n", value);
		return;
	}

#ifdef _WIN
	lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, bsrRegistry, 0, KEY_ALL_ACCESS, &hKey);
	if (ERROR_SUCCESS != lResult) {
		fprintf(stderr, "Failed to RegOpenValueEx status(0x%x)\n", lResult);
		return;
	}
#endif

	if (option_type == PERIOD && value > 0)
#ifdef _WIN
		lResult = RegSetValueEx(hKey, _T("bsrmon_period"), 0, REG_DWORD, (LPBYTE)&option_value, sizeof(option_value));
#else // _LIN
		fp = fopen(PERIOD_OPTION_PATH, "w");
#endif
	else if (option_type == FILE_ROLLING_SIZE && value > 0)
#ifdef _WIN
		lResult = RegSetValueEx(hKey, _T("bsrmon_file_size"), 0, REG_DWORD, (LPBYTE)&option_value, sizeof(option_value));
#else // _LIN
		fp = fopen(FILE_SIZE_OPTION_PATH, "w");
#endif
	else if (option_type == FILE_ROLLING_CNT && value > 0)
#ifdef _WIN
		lResult = RegSetValueEx(hKey, _T("bsrmon_file_cnt"), 0, REG_DWORD, (LPBYTE)&option_value, sizeof(option_value));
#else // _LIN
		fp = fopen(FILE_CNT_OPTION_PATH, "w");
#endif
	else {
#ifdef _WIN
		RegCloseKey(hKey);
#endif
		usage();
	}

#ifdef _WIN
	if (ERROR_SUCCESS != lResult)
		fprintf(stderr, "Failed to RegSetValueEx status(0x%x)\n", lResult);

	RegCloseKey(hKey);
#else // _LIN
	if (fp != NULL) {
		fprintf(fp, "%ld", value);
		fclose(fp);
	}
	else {
		fprintf(stderr, "Failed to open file(%d)\n", option_type);
	}
#endif
}

long GetOptionValue(enum set_option_type option_type)
{
#ifdef _WIN
	HKEY hKey = NULL;
	const TCHAR bsrRegistry[] = _T("SYSTEM\\CurrentControlSet\\Services\\bsr");
	DWORD type = REG_DWORD;
	DWORD size = sizeof(DWORD);
	DWORD lResult = ERROR_SUCCESS;
	DWORD value;

	lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, bsrRegistry, 0, KEY_ALL_ACCESS, &hKey);
	if (ERROR_SUCCESS != lResult) {
		fprintf(stderr, "Failed to RegOpenValueEx status(0x%x)\n", lResult);
		return -1;
	}
#else // _LIN
	FILE *fp;
	long value;
#endif

	if (option_type == PERIOD)
#ifdef _WIN
		lResult = RegQueryValueEx(hKey, _T("bsrmon_period"), NULL, &type, (LPBYTE)&value, &size);
#else // _LIN
		fp = fopen(PERIOD_OPTION_PATH, "r");
#endif
	else if (option_type == FILE_ROLLING_SIZE)
#ifdef _WIN
		lResult = RegQueryValueEx(hKey, _T("bsrmon_file_size"), NULL, &type, (LPBYTE)&value, &size);
#else // _LIN
		fp = fopen(FILE_SIZE_OPTION_PATH, "r");
#endif
	else if (option_type == FILE_ROLLING_CNT)
#ifdef _WIN
		lResult = RegQueryValueEx(hKey, _T("bsrmon_file_cnt"), NULL, &type, (LPBYTE)&value, &size);
#else // _LIN
		fp = fopen(FILE_CNT_OPTION_PATH, "r");
#endif
	else {
#ifdef _WIN
		RegCloseKey(hKey);
#endif
		return -1;
	}

#ifdef _WIN
	if (ERROR_SUCCESS != lResult)
		value = -1;

	RegCloseKey(hKey);
	return value;
#else // _LIN
	if (fp != NULL) {
		fscanf(fp, "%ld", &value);
		fclose(fp);
		return value;
	}
	return -1;
#endif
}

// BSR-788 print bsrmon options value
static void PrintOptionValue(char * option)
{
	bool print_all = false;
	long value = 0;
	if (strcmp(option, "all") == 0) {
		print_all = true;
	} 
	
	if (print_all || strcmp(option, "period") == 0) {
		value = GetOptionValue(PERIOD);
		if (value <= 0)
			value = DEFAULT_BSRMON_PERIOD;
		printf("period : %ld sec\n", value);
	}
	if (print_all || strcmp(option, "file_size") == 0) {
		value = GetOptionValue(FILE_ROLLING_SIZE);
		if (value <= 0)
			value = DEFAULT_FILE_ROLLING_SIZE;
		printf("file_size : %ld MB\n", value);
	}
	if (print_all || strcmp(option, "file_cnt") == 0) {
		value = GetOptionValue(FILE_ROLLING_CNT);
		if (value <= 0)
			value = DEFAULT_FILE_ROLLONG_CNT;
		printf("file_cnt : %ld\n", value);
	}

	if (!value)
		usage();
}

// BSR-695
static void SetBsrmonRun(unsigned int run)
{
#ifdef _WIN
	HANDLE      hDevice = INVALID_HANDLE_VALUE;
	DWORD       dwReturned = 0;
	unsigned int pre_value;
#else // _LIN
	FILE * fp;
	int fd = 0;
#endif

#ifdef _WIN
	hDevice = OpenDevice(MVOL_DEVICE);
	if (hDevice == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Failed to open bsr\n");
		return;
	}

	// BSR-740 send to bsr engine
	if (DeviceIoControl(hDevice, IOCTL_MVOL_SET_BSRMON_RUN, &run, sizeof(unsigned int), &pre_value, sizeof(unsigned int), &dwReturned, NULL) == FALSE) {
		fprintf(stderr, "Failed to IOCTL_MVOL_SET_BSRMON_RUN\n");
	}

	if (hDevice != INVALID_HANDLE_VALUE) {
		CloseHandle(hDevice);
	}

	// BSR-801 if it is the same as the previous value, an error is output
	if (pre_value == run) {
		if (run)
			fprintf(stderr, "Already running\n");
		else
			fprintf(stderr, "bsrmon is not running\n");
	}

#else // _LIN
	if ((fd = open(BSR_CONTROL_DEV, O_RDWR)) == -1) {
		fprintf(stderr, "Can not open /dev/bsr-control\n");
		return;
	}
	// BSR-740 send to bsr engine
 	if (ioctl(fd, IOCTL_MVOL_SET_BSRMON_RUN, &run) != 0) {
		fprintf(stderr, "Failed to IOCTL_MVOL_SET_BSRMON_RUN\n");
	}
	if (fd)
		close(fd);

	// write /etc/bsr.d/.bsrmon_run
	fp = fopen(BSR_MON_RUN_REG, "w");
	if (fp != NULL) {
		fprintf(fp, "%d", run);
		fclose(fp);
	} 
	else 
		fprintf(stderr, "Failed to open %s file\n", BSR_MON_RUN_REG);
#endif
	
}

#ifdef _LIN
static pid_t GetRunningPid() {
	char buf[10] = {0,};
	pid_t pid;
	FILE *cmd_pipe = popen("pgrep bsrmon-run", "r");

	if (!cmd_pipe)
		return 0;
	fgets(buf, MAX_PATH, cmd_pipe);
	pid = strtoul(buf, NULL, 10);
	pclose(cmd_pipe);
	return pid;
}
#endif

static void StartMonitor()
{
	
#ifdef _LIN
	char buf[MAX_PATH] = {0,};
	pid_t pid;
#endif

	InitMonitor();
	SetBsrmonRun(1);

#ifdef _LIN
	pid = GetRunningPid();
	
	if (pid > 0) {
		fprintf(stderr, "Already running (pid=%d)\n", pid);
		return;
	}
	sprintf(buf, "nohup bsrmon-run >/dev/null 2>&1 &");

	if (system(buf) !=0) {
		fprintf(stderr, "Failed \"%s\"\n", buf);
		return;
	}
#endif

}

static void StopMonitor()
{
#ifdef _LIN
	char buf[MAX_PATH] = {0,};
	pid_t pid = GetRunningPid();

	if (pid <= 0) {
		fprintf(stdout, "bsrmon-run is not running\n");
	} else {
		sprintf(buf, "kill -TERM %d >/dev/null 2>&1", pid);

		if (system(buf) !=0)
			fprintf(stderr, "Failed \"%s\"\n", buf);
	}
#endif

	SetBsrmonRun(0);
}

// BSR-764
static int SetPerfSimulFlag(SIMULATION_PERF_DEGR* pt)
{
#ifdef _WIN
	HANDLE      hDevice = INVALID_HANDLE_VALUE;
	DWORD       retVal = ERROR_SUCCESS;
	DWORD       dwReturned = 0;
	BOOL        ret = FALSE;
#else // _LIN
	int fd = 0;
#endif

	int err = 0;

	if (pt == NULL) {
		fprintf(stderr, "LOG_ERROR: %s: Invalid parameter\n", __FUNCTION__);
		return -1;
	}
#ifdef _WIN
	// 1. Open MVOL_DEVICE
	hDevice = OpenDevice(MVOL_DEVICE);
	if (hDevice == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Failed to open bsr\n");
		return -1;
	}

	ret = DeviceIoControl(hDevice, IOCTL_MVOL_SET_SIMUL_PERF_DEGR,
		pt, sizeof(SIMULATION_PERF_DEGR), pt, sizeof(SIMULATION_PERF_DEGR), &dwReturned, NULL);
	if (ret == FALSE) {
		fprintf(stderr, "Failed to IOCTL_MVOL_SET_SIMUL_PERF_DEGR\n");
		err = -1;
	}

	// 3. CloseHandle MVOL_DEVICE
	if (hDevice != INVALID_HANDLE_VALUE) {
		CloseHandle(hDevice);
	}
#else // _LIN
	if ((fd = open(BSR_CONTROL_DEV, O_RDWR)) == -1) {
		fprintf(stderr, "Can not open /dev/bsr-control\n");
		return -1;
	}
	if (ioctl(fd, IOCTL_MVOL_SET_SIMUL_PERF_DEGR, pt) != 0)
		err = 1;

	if (fd)
		close(fd);

#endif

	return err;
}

int ConvertType(char * type_name) 
{
	if (strcmp(type_name, "iostat") == 0)
		return IO_STAT;
	else if (strcmp(type_name, "ioclat") == 0)
		return IO_COMPLETE;
	else if (strcmp(type_name, "reqstat") == 0)
		return REQUEST;
	else if (strcmp(type_name, "peer_reqstat") == 0)
		return PEER_REQUEST;
	else if (strcmp(type_name, "alstat") == 0)
		return AL_STAT;
	else if (strcmp(type_name, "network") == 0)
		return NETWORK_SPEED;
	else if (strcmp(type_name, "sendbuf") == 0)
		return SEND_BUF;
	else if (strcmp(type_name, "memstat") == 0)
		return MEMORY;
	// BSR-838
	else if (strcmp(type_name, "resync_ratio") == 0)
		return RESYNC_RATIO;
	else if (strcmp(type_name, "all") == 0)
		return ALL_STAT;
	
	return -1;
}


#ifdef _WIN
int main(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
	int res = ERROR_SUCCESS;
	int argIndex = 0;

	if (argc < 2)
		usage();

	for (argIndex = 1; argIndex < argc; argIndex++) {
		if (!strcmp(argv[argIndex], "/print")) {
			argIndex++;
			if (argIndex <= argc)
				PrintMonitor();
			else
				usage();
		}
		else if (!strcmp(argv[argIndex], "/file")) {
			argIndex++;
			if (argIndex <= argc) {
				MonitorToFile();
			}
			else
				usage();
		}
		// BSR-772 when the /scroll option is used, the output is scrolled.
		//		default is fixed screen output.
		else if (!strcmp(argv[argIndex], "/watch")) {
			int type = -1;
			char *res_name = NULL;
			bool b_scroll = false;
			int vnr = -1;

			if (++argIndex < argc) {
				type = ConvertType(argv[argIndex]);

				if (type < 0) 
					usage();
			
				if (++argIndex >= argc) {
					if (type != MEMORY)
						usage();
				}

				if (type == MEMORY) {
					if (argIndex < argc) {
						if (!strcmp(argv[argIndex], "/scroll"))
							b_scroll = true;
						else
						usage();
					}
					Watch(NULL, MEMORY, -1, b_scroll);
					break;
				}

				if (argIndex >= argc) 
					usage();
				res_name = argv[argIndex];
				if (type <= REQUEST) {
					if (++argIndex < argc)
						vnr = atoi(argv[argIndex]);
					else
						usage();
				} 

				if (++argIndex < argc) {
					if (!strcmp(argv[argIndex], "/scroll"))
						b_scroll = true;
					else
						usage();
				}

				Watch(res_name, type, vnr, b_scroll);
				break;
			} else
				usage();
		}
		
		else if (!strcmp(argv[argIndex], "/set")) {
			argIndex++;

			if (argIndex < argc) {
				if (strcmp(argv[argIndex], "period") == 0) {
					argIndex++;
					if (argIndex < argc)
						SetOptionValue(PERIOD, atoi(argv[argIndex]));
					else
						usage();
				}
				else if (strcmp(argv[argIndex], "file_size") == 0) {
					argIndex++;
					if (argIndex < argc)
						SetOptionValue(FILE_ROLLING_SIZE, atoi(argv[argIndex]));
					else
						usage();
				}
				else if (strcmp(argv[argIndex], "file_cnt") == 0) {
					argIndex++;
					if (argIndex < argc)
						SetOptionValue(FILE_ROLLING_CNT, atoi(argv[argIndex]));
					else
						usage();
				}
				else
					usage();
			}
			else
				usage();
		}
		// BSR-788
		else if (!strcmp(argv[argIndex], "/get")) {
			argIndex++;

			if (argIndex < argc) {
				PrintOptionValue(argv[argIndex]);
			}
			else
				usage();
		}
		else if (!strcmp(argv[argIndex], "/start")) {
			argIndex++;
			if (argIndex <= argc) {
				StartMonitor();
			}
			else
				usage();
		}
		else if (!strcmp(argv[argIndex], "/stop")) {
			argIndex++;
			if (argIndex <= argc) {
				StopMonitor();
			}
			else
				usage();
		}
		// BSR-741
		else if (!strcmp(argv[argIndex], "/status")) {
			argIndex++;
			if (argIndex <= argc) {
				if (is_running()) {
#ifdef _LIN
					// BSR-796 print whether the bsrmon-run script is running
					pid_t pid = GetRunningPid();
					if (pid > 0)
						fprintf(stderr, "bsrmon-run script is running (pid=%d)\n", pid);
					else
						fprintf(stdout, "bsrmon-run script is not running\n");

#endif					
				}
			}
			else
				usage();
		}
		// BSR-709
		else if (!strcmp(argv[argIndex], "/report")) {
			int type = -1;
			char *res_name = NULL;
			char *file_name = NULL;
			int vol_num = -1;
			struct time_filter tf;

			memset(&tf, 0, sizeof(struct time_filter));

			if (++argIndex < argc) {
				type = ConvertType(argv[argIndex]);

				if (type < 0) {
					usage();
				}
			
				if (type != MEMORY) {
					if (++argIndex >= argc) {
						usage();
					}
					res_name = argv[argIndex];
					if (type <= REQUEST) {
						// IO_STAT, IO_COMPLETE, AL_STAT, PEER_REQUEST, REQUEST need vnr
						if (++argIndex < argc) {
							vol_num = atoi(argv[argIndex]);
						} else {
							usage();
						}
					}
				}

				// BSR-771 add date and time option to /report command
				for (argIndex++; argIndex < argc; argIndex++) {
					if (!strncmp(argv[argIndex], "/", 1)) {
						int c = *(argv[argIndex] + 1);
						if (++argIndex >= argc) {
							usage();
						}
						switch (c) {
						case 'f':
							file_name = argv[argIndex];
							break;
						case 'd':
							tf.date = argv[argIndex];
							break;
						case 's':
							parse_timestamp(argv[argIndex], &tf.start_time, DEF_START_TIME);
							break;
						case 'e':
							parse_timestamp(argv[argIndex], &tf.end_time, DEF_END_TIME);
							break;
						default:
							usage();
						}
					} else {
						usage();
					}
				}

				Report(res_name, file_name, type, vol_num, &tf);
				break;
				
			}
			else {
				usage();
			}
		}
		// BSR-764 add I/O performance degradation simulation
		else if (!strcmp(argv[argIndex], "/io_delay_test"))
		{
			SIMULATION_PERF_DEGR pt;
			argIndex++;
			// get parameter 1 (flag)
			if (argIndex < argc) {
				pt.flag = atoi(argv[argIndex]);
				argIndex++;
				// get parameter 2 (type)
				if (argIndex < argc) {
					pt.type = atoi(argv[argIndex]);
					argIndex++;
					// get parameter 3 (time)
					if (argIndex < argc) {
						pt.delay_time = atoi(argv[argIndex]);
						if (pt.delay_time >= 0 && pt.delay_time <= 500) // 0~500 ms
							SetPerfSimulFlag(&pt);
					}
					else {
						perf_test_usage();
					}
				}
				else if (pt.flag == 0) {
					SetPerfSimulFlag(&pt);
				}
				else {
					perf_test_usage();
				}
			}
			else {
				perf_test_usage();
			}

		}
#ifdef _WIN
		// BSR-37
		else if (!_stricmp(argv[argIndex], "/debug")) {
			argIndex++;
			if (argIndex < argc)
				res = BsrDebug(argc - argIndex, &argv[argIndex]);
			else
				debug_usage();
			break;
		}
#endif
		else {
			printf("Please check undefined arg[%d]=(%s)\n", argIndex, argv[argIndex]);
		}
	}

	return res;
}