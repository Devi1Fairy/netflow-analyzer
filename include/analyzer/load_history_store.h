#ifndef NETFLOW_ANALYZER_LOAD_HISTORY_STORE_H
#define NETFLOW_ANALYZER_LOAD_HISTORY_STORE_H

#include "analyzer/load_history.h"

#define LOAD_STORE_SOURCE_MAX 127U
#define LOAD_STORE_INTERFACE_MAX 63U
#define LOAD_STORE_FILTER_MAX 1023U

typedef struct {
    const char *source_id;
    const char *interface_name;
    const char *filter_expression; /* 无过滤时传""，不传NULL。 */
} load_store_identity_t;

/*
 * Linux/POSIX快照；调用者拥有参数，同一路径只允许一个写入者。
 * identity字符串只接受可打印ASCII；source/interface不能为空。
 * source_id由部署配置提供，标识实际采集来源，不是身份认证。
 *
 * 保存最近最多1008个闭窗的时间顺序、包数和usable标记。
 * usable只保留complete && quality_ok，不保留两者各自的原因。
 * 不保存单调时钟、当前未闭合窗口或libpcap统计基线。
 *
 * save: 同目录临时文件 -> fflush/fsync/fclose -> rename替换。
 * rename前失败不覆盖旧文件；临时文件权限0600。
 * 不同步父目录，不承诺断电后目录项一定持久化。
 *
 * load: 校验完整文件及来源后才发布history；失败保持输出不变。
 * 恢复为等价的时间顺序，环形数组下标不保证与原对象相同。
 * 成功不代表历史新鲜或可直接接入新会话；调度层另行校验。
 *
 * 返回0成功；EINVAL参数/格式/范围错误，EXDEV来源配置不匹配，
 * ENOENT文件不存在，其他值为I/O或系统错误。
 */
int load_history_store_save(
    const char *path,
    const load_store_identity_t *identity,
    const load_history_t *history);

int load_history_store_load(
    const char *path,
    const load_store_identity_t *identity,
    load_history_t *history);

#endif