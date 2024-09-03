#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/time.h>

const int max_tree_elem = 256;
static int PID = -1;
int **process_pids;
long long int **creation_times;

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mete_Erdogan & Sebnem_Demirtas");
MODULE_DESCRIPTION("psvis: a module that returns the child tree of a given root process");

module_param(PID, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(myint, "Entered PID of the root process: \n");


void depth_first_search(struct task_struct *task, int **process_pids, long long int **creation_times, int depth){
	if (!task) return;
	struct list_head *list;
	int j = 0;
	printk(KERN_CONT "depth: %d, ", depth);
	for(int i=0; i<depth; i++) printk(KERN_CONT "-");
	printk(KERN_CONT "PID: %d, Creation Time: %llu ns \n", (int) task->pid, task->start_time);
	list_for_each(list, &task->children) {
		struct task_struct *child = list_entry(list, struct task_struct, sibling);
		process_pids[depth+1][j] = (int)child->pid;
		creation_times[depth+1][j] = child->start_time;
		depth_first_search(child, process_pids, creation_times, depth + 1);
		j++;
	}
}

// A function that runs when the module is first loaded
int simple_init(void) {
	printk(KERN_INFO "Loading the psvis module to the kernel.\n");
	if(PID<0){
		printk(KERN_INFO "Not a valid PID, unloading the module.\n");
		return 1;
	}else {
		struct task_struct *task;
		process_pids = kmalloc(max_tree_elem*sizeof(int*),GFP_KERNEL);
		creation_times = kmalloc(max_tree_elem*sizeof(long long int*),GFP_KERNEL);
		printk(KERN_CONT "\n");
		
		for(int i=0; i<max_tree_elem; i++) {
			process_pids[i] = kcalloc(max_tree_elem*sizeof(int),1,GFP_KERNEL);
			creation_times[i] = kcalloc(max_tree_elem*sizeof(long long int),1,GFP_KERNEL);
		}
		
		task = pid_task(find_vpid((pid_t) PID), PIDTYPE_PID);
		if(task == NULL) {
			printk(KERN_ALERT "Process with PID %d is not found.", PID);
		}else {
			process_pids[0][0] = (int) task->pid;
			creation_times[0][0] = task->start_time;
			depth_first_search(task,process_pids,creation_times,0);
		}
		printk(KERN_CONT "\n");
		return 0;
	}
}

// A function that runs when the module is removed
void simple_exit(void) {
	// free allocated memories
	for(int i = 0; i < max_tree_elem; i++) {
		kfree(process_pids[i]);
		kfree(creation_times[i]);
	}
	
	kfree(process_pids);
	kfree(creation_times);
	
	printk(KERN_INFO "Removing the psvis module from the kernel. \n");
}

module_init(simple_init);
module_exit(simple_exit);
