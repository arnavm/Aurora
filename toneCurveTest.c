#include <stdio.h>


int main() {

	FILE *ifp = fopen("toneCurveRaw", "r");
	float curved;

	while (fscanf(ifp, "%f", &curved) != EOF) {
		fprintf(stdout, "%f\n", curved);
	}
	close(ifp);	
	return 0;
}
