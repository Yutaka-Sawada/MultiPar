
// JSONファイルを開く
void json_open(void);

// JSONファイルを閉じる
void json_close(void);

// ファイル一覧
void json_file_list(file_ctx_r *files);

// ファイルの状態
void json_file_state(file_ctx_r *files);

// 検出されたファイル名を保持する
void json_add_found(wchar_t *filename, int flag_external);

// 検出されたファイル名を書き込む
void json_save_found(void);

