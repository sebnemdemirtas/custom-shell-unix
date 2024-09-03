#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#define MAX_DEPTH 10
#define MAX_NODES 100

int depthmax = 0;
long long int timemin = 99999999999999;

typedef struct {
	int pid;
	long long int creation_time;
	int depth;
	int children[MAX_DEPTH];
	int num_children;
} Node;

Node nodes[MAX_NODES];
int num_nodes = 0;

void read_tree_from_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error opening file.\n");
        exit(1);
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
    	if(strstr(line, "depth: ")==NULL || strstr(line, "Creation Time: ")==NULL || strstr(line, "PID: ")==NULL) {continue;}
        int depth = 0;
        for(int i=0;i<(int) strlen(line);i++) if(line[i] == '-') depth++;
        Node node;
        char* pid_str = strstr(line, "PID:");
        if (pid_str) sscanf(pid_str, "PID: %d, Creation Time: %lld", &node.pid, &node.creation_time);
        node.depth = depth;
        if(depth > depthmax) depthmax = depth;
        if(node.creation_time < timemin) timemin = node.creation_time;
        node.num_children = 0;
        nodes[num_nodes++] = node;
    }
    fclose(file);
}

void find_children() {
    for (int i = 0; i < num_nodes; i++) {
        for (int j = i + 1; j < num_nodes; j++) {
            if(nodes[j].depth == nodes[i].depth) break;
            if (nodes[j].depth == nodes[i].depth + 1) nodes[i].children[nodes[i].num_children++] = j;
        }
    }
}

void plot_graph(char *filename) {
    FILE* gnuplotPipe = popen("gnuplot -persistent", "w");
    if (!gnuplotPipe) {
        fprintf(stderr, "Error opening gnuplot.\n");
        exit(1);
    }
    fprintf(gnuplotPipe, "set term png size 1000,1000\n");
    fprintf(gnuplotPipe, "set output '%s'\n", filename);
    fprintf(gnuplotPipe, "set xrange [-0.5:%d]\n", (depthmax + 1));
    fprintf(gnuplotPipe, "set yrange [%d:-0.5]\n", num_nodes);
    fprintf(gnuplotPipe, "set style fill solid\n");
    fprintf(gnuplotPipe, "set xlabel 'Depth'\n");
    fprintf(gnuplotPipe, "set ylabel 'Node'\n");

    for (int i = 0; i < num_nodes; i++) {
    	if(i==0) {
    		fprintf(gnuplotPipe, "set object circle at %d, %d size char 4 fillcolor 'red' lw 2\n", nodes[i].depth, i);
    		fprintf(gnuplotPipe, "set object circle at %d, %d size char 3.5 fillcolor 'yellow' lw 2\n", nodes[i].depth, i);
    	}
        if (nodes[i].num_children > 0) {
            for(int j=0; j<nodes[i].num_children;j++){
            	fprintf(gnuplotPipe, "set arrow from %d, %d to %d, %d nohead lw 2 lc rgb 'blue'\n",
                    nodes[i].depth, i, nodes[nodes[i].children[j]].depth, nodes[i].children[j]);
            	if(j==0) fprintf(gnuplotPipe, "set object circle at %d, %d size char 3 fillcolor 'green' lw 2\n", nodes[nodes[i].children[j]].depth, nodes[i].children[j]);
            }
        }
        fprintf(gnuplotPipe, "set object circle at %d, %d size char 2 fillcolor 'yellow' lw 2\n", nodes[i].depth, i);
        fprintf(gnuplotPipe, "set label 'pid:%d time:%lld' at %d, %d center\n", nodes[i].pid, nodes[i].creation_time-timemin, nodes[i].depth, i);
    }
    fprintf(gnuplotPipe, "plot NaN notitle\n");
    pclose(gnuplotPipe);
}

