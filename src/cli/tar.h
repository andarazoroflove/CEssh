#ifndef CESSH_TAR_H
#define CESSH_TAR_H

/* Create tape archive: tarc <archive.tar> <file1> [file2...] */
int tar_create_cmd(int argc, char **argv);

/* Extract tape archive: tarx <archive.tar> */
int tar_extract_cmd(int argc, char **argv);

#endif /* CESSH_TAR_H */
