
// SFV ファイル
int verify_sfv(
	char *ascii_buf,
	wchar_t *file_path);

// MD5 ファイル
int verify_md5(
	char *ascii_buf,
	wchar_t *file_path);

// FLAC Fingerprint ファイル
int verify_ffp(
	char *ascii_buf,
	wchar_t *file_path);

// Cryptography API: Next Generation が対応するハッシュ
int verify_cng(
	int hash_format,
	char *ascii_buf,
	wchar_t *file_path);

