#ifndef CONFIGFILE_H
#define CONFIGFILE_H

struct file_list {
	char* file;
	struct file_list* next;
};

struct config_entry {
	char* name;
	char* rootdir;
	unsigned int personality;
	struct file_list* files_to_copy;
	struct config_entry* next;
};

void
free_file_list(struct file_list* list);

void
free_config_entry(struct config_entry* entry);

void
free_config_entries(struct config_entry* entries);

int
parse_configfile(int fd, struct config_entry** entries);

#endif // CONFIGFILE_H
