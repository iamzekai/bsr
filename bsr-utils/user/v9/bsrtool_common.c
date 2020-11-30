#define _GNU_SOURCE
#define _XOPEN_SOURCE 600
#define _FILE_OFFSET_BITS 64

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <ctype.h>
#include <bsr.h>
#ifdef _WIN
#include <windows.h>
#else // _LIN
#include <linux/fs.h>           /* for BLKGETSIZE64 */
#include <time.h>
#endif
#include <string.h>
#include <netdb.h>

#include<dirent.h>
#include<sys/types.h>

#include "linux/bsr_config.h"
#include "bsrtool_common.h"
#include "config.h"
#include "../../bsr-headers/bsr_log.h"

static struct version __bsr_driver_version = {};
static struct version __bsr_utils_version = {};

char *lprogram = NULL;
char *lcmd = NULL;
int llevel = INFO_LEVEL;

void dt_pretty_print_uuids(const uint64_t* uuid, unsigned int flags)
{
	printf(
"\n"
"       +--<  Current data generation UUID  >-\n"
"       |               +--<  Bitmap's base data generation UUID  >-\n"
"       |               |                 +--<  younger history UUID  >-\n"
"       |               |                 |         +-<  older history  >-\n"
"       V               V                 V         V\n");
	dt_print_uuids(uuid, flags);
	printf(
"                                                                    ^ ^ ^ ^ ^ ^ ^\n"
"                                      -<  Data consistency flag  >--+ | | | | | |\n"
"                             -<  Data was/is currently up-to-date  >--+ | | | | |\n"
"                                  -<  Node was/is currently primary  >--+ | | | |\n"
"                                  -<  Node was/is currently connected  >--+ | | |\n"
"         -<  Node was in the progress of setting all bits in the bitmap  >--+ | |\n"
"                        -<  The peer's disk was out-dated or inconsistent  >--+ |\n"
"      -<  This node was a crashed primary, and has not seen its peer since   >--+\n"
"\n");
	printf("flags:%s %s, %s, %s%s%s\n",
	       (flags & MDF_CRASHED_PRIMARY) ? " crashed" : "",
	       (flags & MDF_PRIMARY_IND) ? "Primary" : "Secondary",
	       (flags & MDF_CONNECTED_IND) ? "Connected" : "StandAlone",
	       (flags & MDF_CONSISTENT)
			?  ((flags & MDF_WAS_UP_TO_DATE) ? "UpToDate" : "Outdated")
			: "Inconsistent",
	       (flags & MDF_FULL_SYNC) ? ", need full sync" : "",
	       (flags & MDF_PEER_OUT_DATED) ? ", peer Outdated" : "");
	printf("meta-data: %s\n", (flags & MDF_AL_CLEAN) ? "clean" : "need apply-al");
}

void dt_print_v9_uuids(const uint64_t* uuid, unsigned int mdf_flags, unsigned int mdf_peer_flags)
{
	int i;
	printf(X64(016)":"X64(016)":",
	       uuid[UI_CURRENT],
	       uuid[UI_BITMAP]);
	for ( i=UI_HISTORY_START ; i<=UI_HISTORY_END ; i++ ) {
		printf(X64(016)":", uuid[i]);
	}
	printf("%d:%d:%d:%d:%d:%d",
	       mdf_flags & MDF_CONSISTENT ? 1 : 0,
	       mdf_flags & MDF_WAS_UP_TO_DATE ? 1 : 0,
	       mdf_flags & MDF_PRIMARY_IND ? 1 : 0,
	       mdf_flags & MDF_CRASHED_PRIMARY ? 1 : 0,
	       mdf_flags & MDF_AL_CLEAN ? 1 : 0,
	       mdf_flags & MDF_AL_DISABLED ? 1 : 0);	

	printf(":%d:%d:%d:%d:%d\n",
	       mdf_peer_flags & MDF_PEER_CONNECTED ? 1 : 0,
	       mdf_peer_flags & MDF_PEER_OUTDATED ? 1 : 0,
		   mdf_peer_flags & MDF_PEER_FENCING ? 1 : 0, 
		   mdf_peer_flags & MDF_PEER_FULL_SYNC ? 1 : 0,
		   mdf_flags & MDF_LAST_PRIMARY ? 1 : 0 // DW-1291 provide LastPrimary Information.
		   );
}

void dt_pretty_print_v9_uuids(const uint64_t* uuid, unsigned int mdf_flags, unsigned int mdf_peer_flags)
{
	printf(
"\n"
"       +--<  Current data generation UUID  >-\n"
"       |               +--<  Bitmap's base data generation UUID  >-\n"
"       |               |                 +--<  younger history UUID  >-\n"
"       |               |                 |         +-<  older history  >-\n"
"       V               V                 V         V\n");
	dt_print_v9_uuids(uuid, mdf_flags, mdf_peer_flags);

	printf(
		"                                                                    ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^\n"
		"                                      -<  Data consistency flag  >--+ | | | | | | | | | |\n"
		"                             -<  Data was/is currently up-to-date  >--+ | | | | | | | | |\n"
		"                                  -<  Node was/is currently primary  >--+ | | | | | | | |\n"
		" -<  This node was a crashed primary, and has not seen its peer since  >--+ | | | | | | |\n"
		"             -<  The activity-log was applied, the disk can be attached  >--+ | | | | | |\n"
		"        -<  The activity-log was disabled, peer is completely out of sync  >--+ | | | | |\n"
		"                                        -<  Node was/is currently connected  >--+ | | | |\n"
		"                            -<  The peer's disk was out-dated or inconsistent  >--+ | | |\n"
		"                               -<   A fence policy other the dont-care was used  >--+ | |\n"
		"                -<  Node was in the progress of marking all blocks as out of sync  >--+ |\n"
		"                                                     -<  Node was/is a Last Primary  >--+\n" // DW-1291 provide LastPrimary Information.
		"\n");
}



const char *get_hostname(void)
{
	static char *s_hostname;

	if (!s_hostname) {
		char hostname[HOST_NAME_MAX];

		if (gethostname(hostname, sizeof(hostname))) {
			CLI_ERRO_LOG_PEEROR(false, hostname);
			exit(20);
		}
		s_hostname = strdup(hostname);
	}
	return s_hostname;
}


#ifdef _WIN 
typedef struct _MVOL_VOLUME_INFO
{
	BOOLEAN				Active;
	WCHAR				PhysicalDeviceName[256];		// src device
	ULONG				PeerIp;
	USHORT				PeerPort;
	CHAR				Seq[2048]; // BSR_DW130: check enough? and chaneg to dynamically
} MVOL_VOLUME_INFO, *PMVOL_VOLUME_INFO;

#define	MVOL_TYPE		0x9800
#define	IOCTL_MVOL_GET_PROC_BSR			CTL_CODE(MVOL_TYPE, 38, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif 

/* For our purpose (finding the revision) SLURP_SIZE is always enough.
 */
static char *slurp_proc_bsr()
{
#ifdef _WIN
	HANDLE hDevice = INVALID_HANDLE_VALUE;
	MVOL_VOLUME_INFO VolumeInfo = { 0, };
	char *buffer = NULL;
	DWORD dwReturned = 0;
	const int SLURP_SIZE = 4096;
	BOOL ret = FALSE;

	buffer = malloc(SLURP_SIZE);
	if (!buffer)   return NULL;

	hDevice = CreateFileA("\\\\.\\mvolBsrCtrl", GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		free(buffer);
		return NULL;
	}

	ret = DeviceIoControl(hDevice, IOCTL_MVOL_GET_PROC_BSR,
		NULL, 0, &VolumeInfo, sizeof(MVOL_VOLUME_INFO), &dwReturned, NULL);
	if (ret == FALSE) {
		CloseHandle(hDevice);
		free(buffer);
		return NULL;
	}

	CloseHandle(hDevice);
	memcpy(buffer, VolumeInfo.Seq, SLURP_SIZE);
	return buffer;
#else // _LIN
	const int SLURP_SIZE = 4096;
	char *buffer;
	int rr, fd;

	fd = open("/proc/bsr",O_RDONLY);
	if (fd == -1)
		return NULL;

	buffer = malloc(SLURP_SIZE);
	if(!buffer)
		goto fail;

	rr = read(fd, buffer, SLURP_SIZE-1);
	if (rr == -1) {
		free(buffer);
		buffer = NULL;
		goto fail;
	}

	buffer[rr]=0;
fail:
	close(fd);

	return buffer;
#endif 
}

static void read_hex(char *dst, char *src, int dst_size, int src_size)
{
	int dst_i, u, src_i=0;

	for (dst_i=0; dst_i < dst_size; dst_i++) {
		if (src[src_i] == 0) break;
		if (src_size - src_i < 2) {
			sscanf(src+src_i, "%1x", &u);
			dst[dst_i] = u << 4;
		} else {
			sscanf(src+src_i, "%2x", &u);
			dst[dst_i] = u;
		}
		if (++src_i >= src_size)
			break;
		if (src[src_i] == 0)
			break;
		if (++src_i >= src_size)
			break;
	}
}

static void version_from_str(struct version *rel, const char *token)
{
	char *dot;
	long maj, min, sub, pat;
	maj = strtol(token, &dot, 10);
	if (*dot != '.')
		return;
	min = strtol(dot+1, &dot, 10);
	if (*dot != '.') {
		// BSR-713 output as 0 if there is no sub version
		sub = 0;
	} else 
		sub = strtol(dot+1, &dot, 10);
	
	if (*dot != '.') {
		// BSR-713 output as 0 if there is no patch version
		pat = 0;
	} else
		pat = strtol(dot+1, &dot, 10);
	/* don't check on *dot == 0,
	 * we may want to add some extraversion tag sometime
	if (*dot != 0)
		return;
	*/

	rel->version.major = maj;
	rel->version.minor = min;
	rel->version.sublvl = sub;
	rel->version.patch = pat;

	rel->version_code = (maj << 24) + (min << 16) + (sub << 8) + pat;
}

static void parse_version(struct version *rel, const char *text)
{
	char token[80];
	int plus=0;
	enum { BEGIN, F_VER, F_SVN, F_REV, F_GIT, F_SRCV } ex = BEGIN;

	while (sget_token(token, sizeof(token), &text) != EOF) {
		switch(ex) {
		case BEGIN:
			if (!strcmp(token, "BSR:"))
				ex = F_VER;
			if (!strcmp(token, "SVN"))
				ex = F_SVN;
			if (!strcmp(token, "GIT-hash:"))
				ex = F_GIT;
			if (!strcmp(token, "srcversion:"))
				ex = F_SRCV;
			break;
		case F_VER:
			if (!strcmp(token, "plus")) {
				plus = 1;
				/* still waiting for version */
			} else {
				version_from_str(rel, token);
				ex = BEGIN;
			}
			break;
		case F_SVN:
			if (!strcmp(token,"Revision:"))
				ex = F_REV;
			break;
		case F_REV:
			rel->svn_revision = atol(token) * 10;
			if (plus)
				rel->svn_revision += 1;
			memset(rel->git_hash, 0, GIT_HASH_BYTE);
			return;
		case F_GIT:
			read_hex(rel->git_hash, token, GIT_HASH_BYTE, strlen(token));
			rel->svn_revision = 0;
			return;
		case F_SRCV:
			memset(rel->git_hash, 0, SRCVERSION_PAD);
			read_hex(rel->git_hash + SRCVERSION_PAD, token, SRCVERSION_BYTE, strlen(token));
			rel->svn_revision = 0;
			return;
		}
	}
}

const struct version *bsr_driver_version(enum driver_version_policy fallback)
{
	char *version_txt;
	char *bsr_driver_version_override;

	if (__bsr_driver_version.version_code)
		return &__bsr_driver_version;

	bsr_driver_version_override = getenv("BSR_DRIVER_VERSION_OVERRIDE");
	if (bsr_driver_version_override) {
		version_from_str(&__bsr_driver_version, bsr_driver_version_override);
		if (__bsr_driver_version.version_code)
			return &__bsr_driver_version;
	}

	version_txt = slurp_proc_bsr();
	if (version_txt) {
		parse_version(&__bsr_driver_version, version_txt);
		free(version_txt);
		return &__bsr_driver_version;
	} else {
		FILE *in = popen("modinfo -F version bsr", "r");
		if (in) {
			char buf[32];
			int c = fscanf(in, "%30s", buf);
			pclose(in);
			if (c == 1) {
				version_from_str(&__bsr_driver_version, buf);
				return &__bsr_driver_version;
			}
		}
	}

	if (fallback == FALLBACK_TO_UTILS)
		return bsr_utils_version();

	return NULL;
}

const struct version *bsr_utils_version(void)
{
	if (!__bsr_utils_version.version_code) {
		version_from_str(&__bsr_utils_version, PACKAGE_VERSION);
		parse_version(&__bsr_utils_version, bsr_buildtag());
	}

	return &__bsr_utils_version;
}

int version_code_kernel(void)
{
	const struct version *driver_version = bsr_driver_version(_STRICT);
	return driver_version ? driver_version->version_code : 0;
}

const char *escaped_version_code_kernel(void)
{
	const struct version *driver_version = bsr_driver_version(_STRICT);
	char buf[32];

	if (!driver_version)
		return "0";

	snprintf(buf, sizeof(buf), "%u.%u.%u",
		driver_version->version.major, driver_version->version.minor,
		driver_version->version.sublvl);

	/* keep the shell_escape (or change the code), otherwise you don't have a static buffer */
	return shell_escape(buf);
}

int version_code_userland(void)
{
	const struct version *utils_version = bsr_utils_version();
	return utils_version->version_code;
}

int version_equal(const struct version *rev1, const struct version *rev2)
{
	if( rev1->svn_revision || rev2->svn_revision ) {
		return rev1->svn_revision == rev2->svn_revision;
	} else {
		return !memcmp(rev1->git_hash,rev2->git_hash,GIT_HASH_BYTE);
	}
}
void config_help_legacy(const char * const tool,
		const struct version * const driver_version)
{
	CLI_ERRO_LOG_STDERR(false, tool ,
			"This %s was build without support for bsr kernel code (%d.%d).\n"
			"Consider to rebuild your user land tools\n"
			"and configure --with-%d%dsupport ...\n",
			tool,
			driver_version->version.major, driver_version->version.minor,
			driver_version->version.major, driver_version->version.minor);
}

void add_lib_bsr_to_path(void)
{
	char *new_path = NULL;
	char *old_path = getenv("PATH");
	static const char lib_bsr[]="/lib/bsr";

	if (!old_path)
		setenv("PATH", lib_bsr, 1);
	else {
		m_asprintf(&new_path, "%s%s%s",
				old_path,
				(*old_path &&
				 old_path[strlen(old_path) -1] != ':')
				? ":" : "",
				lib_bsr);
		setenv("PATH", new_path, 1);
	}
}

/* from linux/crypto/crc32.c */
static const uint32_t crc32c_table[256] = {
	0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
	0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
	0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
	0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
	0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
	0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
	0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
	0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
	0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
	0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
	0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
	0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
	0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
	0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
	0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
	0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
	0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
	0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
	0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
	0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
	0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
	0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
	0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
	0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
	0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
	0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
	0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
	0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
	0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
	0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
	0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
	0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
	0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
	0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
	0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
	0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
	0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
	0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
	0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
	0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
	0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
	0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
	0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
	0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
	0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
	0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
	0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
	0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
	0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
	0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
	0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
	0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
	0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
	0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
	0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
	0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
	0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
	0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
	0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
	0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
	0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
	0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
	0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
	0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L
};

/*
 * Steps through buffer one byte at at time, calculates reflected
 * crc using table.
 */

uint32_t crc32c(uint32_t crc, const uint8_t *data, unsigned int length)
{
	while (length--)
		crc = crc32c_table[(crc ^ *data++) & 0xFFL] ^ (crc >> 8);

	return crc;
}

DWORD get_cli_log_file_max_count()
{
	DWORD cli_log_file_max_count = 0;

#ifdef _WIN
	DWORD lResult = ERROR_SUCCESS;
	HKEY hKey = NULL;
	const char bsrRegistry[] = "SYSTEM\\CurrentControlSet\\Services\\bsr";
	DWORD type = REG_DWORD;
	DWORD size = sizeof(DWORD);

	lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, bsrRegistry, 0, KEY_ALL_ACCESS, &hKey);
	if (ERROR_SUCCESS != lResult) {
		return FALSE;
	}

	lResult = RegQueryValueEx(hKey, BSR_CLI_LOG_FILE_MAX_COUT_VALUE_REG, NULL, &type, (LPBYTE)&cli_log_file_max_count, &size);
	RegCloseKey(hKey);

	if (lResult == ERROR_SUCCESS) {
		return cli_log_file_max_count;
	}
#else // _LIN
	FILE *fp;

	fp = fopen(BSR_CLI_LOG_FILE_MAXCNT_REG, "r");
	if (fp != NULL) {
		char buf[10] = { 0 };
		if (fgets(buf, sizeof(buf), fp) != NULL) {
			cli_log_file_max_count = atoi(buf);
			return cli_log_file_max_count;
		}
		fclose(fp);
	}
#endif
	cli_log_file_max_count = (2 << BSR_ADM_LOG_FILE_MAX_COUNT);
	cli_log_file_max_count += (2 << BSR_SETUP_LOG_FILE_MAX_COUNT);
	cli_log_file_max_count += (2 << BSR_META_LOG_FILE_MAX_COUNT);

	return cli_log_file_max_count;
}

// BSR-605 sort sequentially by file name
void sequential_sort(char buffer[][256], int count)
{
	char temp[256];
	int j = 0, i = 0;

	// BSR-636 all elements must be sorted.
	for (i = 0; i <= count; i++) {
		for (j = i; j <= count; j++) {
			if (strcmp(buffer[i], buffer[j]) == -1) {
				memset(temp, 0, sizeof(temp));
				memcpy(temp, buffer[i], strlen(buffer[i]));
				memset(buffer[i], 0, sizeof(buffer[i]));
				memcpy(buffer[i], buffer[j], strlen(buffer[j]));
				memcpy(buffer[j], temp, strlen(temp));
			}
		}
	}
}

// BSR-605 delete if there are more saved files than the maximum number of files set.
void bsr_max_log_file_check_and_delete(char* fileFullPath)
{
	DIR *dp = NULL;
	struct dirent* entry = NULL;
	char path[256];
	char* ptr;
	int i = 0;
	
	char fileName[256];
	char removeFileFullPath[256];
	int fileMaxCount = CLI_LOG_FILE_MAX_DEFAULT_COUNT;

	if (strstr(lprogram, "bsradm"))
		fileMaxCount = ((get_cli_log_file_max_count() >> BSR_ADM_LOG_FILE_MAX_COUNT) & BSR_LOG_MAX_FILE_COUNT_MASK);
	else if (strstr(lprogram, "bsrsetup"))
		fileMaxCount = ((get_cli_log_file_max_count() >> BSR_SETUP_LOG_FILE_MAX_COUNT) & BSR_LOG_MAX_FILE_COUNT_MASK);
	else if (strstr(lprogram, "bsrmeta"))
		fileMaxCount = ((get_cli_log_file_max_count() >> BSR_META_LOG_FILE_MAX_COUNT) & BSR_LOG_MAX_FILE_COUNT_MASK);

	memset(path, 0, sizeof(path));
	memset(fileName, 0, sizeof(fileName));

#ifdef _WIN
	ptr = strrchr(fileFullPath, '\\');
#else // _LIN
	ptr = strrchr(fileFullPath, '/');
#endif
	memcpy(path, fileFullPath, (ptr - fileFullPath));
	// BSR-621 invalid rolling file name
	snprintf(fileName, strlen(ptr) + 1, "%s_", ptr + 1);

	if ((dp = opendir(path)) == NULL) {
		printf("failed to open %s\n", path);
	}
	else {
		char targetFiles[fileMaxCount + 1][256];
		int fileFindCount = 0;

		for (i = 0; i < fileMaxCount + 1; i++)
			memset(targetFiles[i], 0, sizeof(targetFiles[i]));

		while ((entry = readdir(dp)) != NULL) {
			if (strstr(entry->d_name, fileName)) {
				// BSR-618 delete all saved rolling files
				if (fileMaxCount == 0) {
#ifdef _WIN
					snprintf(removeFileFullPath, sizeof(removeFileFullPath), "%s\\%s", path, entry->d_name); 
#else // _LIN
					snprintf(removeFileFullPath, sizeof(removeFileFullPath), "%s/%s", path, entry->d_name);
#endif
					remove(removeFileFullPath);
				}
				else {
					memcpy(targetFiles[fileFindCount], entry->d_name, sizeof(entry->d_name));
					fileFindCount = fileFindCount + 1;
					if (fileFindCount > fileMaxCount) {
						sequential_sort(targetFiles, fileMaxCount);
						fileFindCount = fileFindCount - 1;
						memset(removeFileFullPath, 0, sizeof(removeFileFullPath));
#ifdef _WIN
						snprintf(removeFileFullPath, sizeof(removeFileFullPath), "%s\\%s", path, targetFiles[fileFindCount]);
#else // _LIN
						snprintf(removeFileFullPath, sizeof(removeFileFullPath), "%s/%s", path, targetFiles[fileFindCount]);
#endif
						remove(removeFileFullPath);
					}
				}
			}
		}
	
		closedir(dp);
	}
}

// BSR-605 if the file is larger than CLI_LOG_FILE_MAX_SIZE(50M), rename it and save it.
bool bsr_log_rolling(char* fileFullPath)
{
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	char fileName[512];
	int res;

	snprintf(fileName, sizeof(fileName), "%s_%04d-%02d-%02dT%02d%02d%02d",
		fileFullPath, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	res = rename(fileFullPath, fileName);
	if (res == -1) {
		printf("failed to log file rename %s => %s\n", fileFullPath, fileName);
		return false;
	}
	
	return true;

}

FILE *bsr_open_log()
{
	char fileFullPath[256];
	FILE* fp = NULL;

	memset(fileFullPath, 0, sizeof(fileFullPath));
#ifdef _WIN
	char *s;
	char *ptr;

	s = getenv("BSR_PATH");

	if (s != NULL) {
		ptr = strrchr(s, L'\\');
		if (s != NULL) {
			memcpy(fileFullPath, s, (ptr - s));
			if (lprogram)
				snprintf(fileFullPath, sizeof(fileFullPath), "%s\\log\\%s.log", fileFullPath, lprogram);
			else
				snprintf(fileFullPath, sizeof(fileFullPath), "%s\\log\\bsrapp.log", fileFullPath);
		}
	}
#else // _LIN
	if (lprogram)
		snprintf(fileFullPath, sizeof(fileFullPath), "/var/log/bsr/%s.log", lprogram);
	else
		snprintf(fileFullPath, sizeof(fileFullPath), "/var/log/bsr/bsrapp.log");
#endif

	fp = fopen(fileFullPath, "a");

	if (!fp)
		printf("log file open failed, %s\n", fileFullPath);
	else {
		off_t size;

		fseeko(fp, 0, SEEK_END);
		size = ftello(fp);

		if (CLI_LOG_FILE_MAX_SIZE < size) {
			fclose(fp);

			if (bsr_log_rolling(fileFullPath))
				fp = fopen(fileFullPath, "a");
		}
		
		bsr_max_log_file_check_and_delete(fileFullPath);
	}

	return fp;
}

long bsr_log_format(char* b, const char* func, int line, enum cli_log_level level)
{
	long offset = 0;
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	offset = snprintf(b, 512, "%04d/%02d/%02d %02d:%02d:%02d ",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	switch (level) {
	case ERROR_LEVEL:
		memcpy(b + offset, "bsr_erro ", LEVEL_OFFSET); break;
	case WARNING_LEVEL:
		memcpy(b + offset, "bsr_warn ", LEVEL_OFFSET); break;
	case INFO_LEVEL:
		memcpy(b + offset, "bsr_info ", LEVEL_OFFSET); break;
	case TRACE_LEVEL:
		memcpy(b + offset, "bsr_trac ", LEVEL_OFFSET); break;
	default:
		memcpy(b + offset, "bsr_unkn ", LEVEL_OFFSET); break;
	}

	offset += LEVEL_OFFSET;
	// BSR-622
	offset += snprintf(b + offset, 512 - offset, "[pid:%d][func:%s][line:%d][cmd:%s] ", getpid(), func, line, ((lcmd == NULL) ? "NULL" : lcmd));
	
	return offset;
}

void bsr_write_log(const char* func, int line, enum cli_log_level level, bool write_continued, bool line_break, const char* fmt, ...)
{
	char b[514];
	long offset = 0;
	va_list args;

	// BSR-614
	if (level > llevel)
		return;

	FILE *fp = bsr_open_log();

	if (fp == NULL) {
		return;
	}

	memset(b, 0, sizeof(b));

	if (!write_continued)
		offset = bsr_log_format(b, func, line, level);

	va_start(args, fmt);
	vsnprintf(b + offset, 512 - offset, fmt, args);
	va_end(args);

	// BSR-671
	fprintf(fp, "%s", b);

	if (line_break) {
#ifdef _WIN
		fprintf(fp, "\r\n");
#else
		fprintf(fp, "\n");
#endif
	}

	fclose(fp);
}

void bsr_write_vlog(const char* func, int line, enum cli_log_level level, const char *fmt, va_list args)
{
	char b[514];
	long offset = 0;

	FILE *fp = bsr_open_log();

	if (fp == NULL) {
		return;
	}

	memset(b, 0, sizeof(b));

	offset = bsr_log_format(b, func, line, level);
	
	vsnprintf(b + offset, 512 - offset, fmt, args);

	// BSR-671
#ifdef _WIN
	fprintf(fp, "%s\r\n", b);
#else
	fprintf(fp, "%s\n", b);
#endif
	fclose(fp);
}

void bsr_exec_log(int argc, char** argv)
{
	int i = 0;
	char b[512];
	int offset = 0;

	memset(b, 0, sizeof(b));

	for (i = 0; i < argc; i++) 
		offset += snprintf(b + offset, 512 - offset, " %s", argv[i]);
	CLI_INFO_LOG(false, "execution command,%s", b);
}
void bsr_terminate_log(int rv)
{
	CLI_INFO_LOG(false, "terminate, rv(%d)", rv);
}