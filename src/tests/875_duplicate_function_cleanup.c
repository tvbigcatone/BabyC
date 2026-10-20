/* A duplicate definition must release the uncommitted FuncValue on parse failure. */
int duplicate_cleanup_target(void) { return 1; }
int duplicate_cleanup_target(void) { return 2; }
