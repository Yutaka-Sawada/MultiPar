
// SFV ファイル
int create_sfv(
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	unsigned int *time_last,
	__int64 *prog_end,		// 経過表示での終了位置
	__int64 total_size);	// 合計ファイル・サイズ

// MD5 ファイル
int create_md5(
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	unsigned int *time_last,
	__int64 *prog_end,		// 経過表示での終了位置
	__int64 total_size);	// 合計ファイル・サイズ

// FLAC Fingerprint ファイル
int create_ffp(
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	__int64 *prog_end);		// 経過表示での終了位置

