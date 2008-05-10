int dcc_cleanup_dotd(const char *dotd_fname, 
                     char **new_dotd_fname,
                     const char *root_dir, 
                     const char *client_out_name, 
                     const char *server_out_name);

int dcc_get_dotd_info(char **argv, char **dotd_fname, 
                      int *needs_dotd, int *sets_dotd_target,
                      char **dotd_target);
