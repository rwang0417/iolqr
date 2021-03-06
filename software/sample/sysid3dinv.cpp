/*  Copyright  2018, Roboti LLC

    This file is licensed under the MuJoCo Resource License (the "License").
    You may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        https://www.roboti.us/resourcelicense.txt
*/

#include <windows.h>
#include <thread>
#include "funclib.h"

//-------------------------------- global variables -------------------------------------
// constants
extern const int kMaxStep = 3000;   // max step number for one rollout
extern const int kMaxState = 160;	// max (state dimension, actuator number)
const int kTestNum = 100;	        // number of monte-carlo runs
const int kMaxThread = 8;           // max thread number

// extern model specific parameters
extern int integration_per_step;
extern int stepnum;
extern int actuatornum;
extern int quatnum;
extern int dof;
extern int modelid;
extern mjtNum control_timestep;
extern mjtNum simulation_timestep;
extern mjtNum state_nominal[kMaxStep][kMaxState];
extern mjtNum ctrl_nominal[kMaxStep * kMaxState];
extern char testmode[30];

// user data and other training settings
mjtNum matAB_check[kMaxStep][kMaxState][kMaxState + kMaxState] = { 0 };
mjtNum dx_estimate[kMaxStep][kMaxState] = { 0 };
mjtNum dx_input[kMaxStep][kMaxState + kMaxState] = { 0 };
mjtNum dx_simulate[kMaxStep][kMaxState] = { 0 };
mjtNum ctrl_max = 0;
mjtNum sysiderr = 0;
mjtNum perturb_coefficient_sysid;
FILE *filestream3;
char data_buff[30], idstr[10];
char keyfilename[100];
char datafilename[100];
char modelfilename[100];
char username[30];
char modelname[30];
char keyfilepre[20] = "";

// model and per-thread data
mjModel* m = NULL;
mjData* d[kMaxThread];

// per-thread statistics
int contacts[kMaxThread];
int constraints[kMaxThread];
double simtime[kMaxThread];

// timer
chrono::system_clock::time_point tm_start;
mjtNum gettm(void)
{
    chrono::duration<double> elapsed = chrono::system_clock::now() - tm_start;
    return elapsed.count();
}


// deallocate and print message
int finish(const char* msg = NULL, mjModel* m = NULL)
{
    // deallocate model
    if( m )
        mj_deleteModel(m);
    mj_deactivate();

    // print message
    if( msg )
        printf("%s\n", msg);

    return 0;
}

// check the accuracy of the identified system
void sysidCheck(mjModel* m, mjData* d)
{
	mjtNum temp0[4] = { 0 };

	for (int t = 0; t < kTestNum; t++)
	{
		// generate perturbation
		for (int i = 0; i < 2*dof + quatnum + actuatornum; i++)
		{
			for (int m = 0; m < stepnum; m++) dx_input[m][i] = 0.01 * ctrl_max * randGauss(0, 1);
		}

		// result from the identified system
		if (modelid == 10) {
			for (int j = 0; j < stepnum; j++) {
				for (int y = 0; y < 4; y++) temp0[y] = 0.0005 * randGauss(0, 1) + state_nominal[j][y + 3];
				mju_normalize4(temp0);
				for (int y = 0; y < 4; y++) dx_input[j][y + 3] = temp0[y] - state_nominal[j][y + 3];
				//mju_mulMatVec(dx_estimate[j], *matAB_check[j], dx_input[j], kMaxState, kMaxState + kMaxState);
			}
		}
		else if (modelid == 11) {
			for (int j = 0; j < stepnum; j++) {
				for (int q = 0; q < quatnum; q++) {
					for (int y = 0; y < 4; y++) temp0[y] = 0.0005 * randGauss(0, 1) + state_nominal[j][y + 4 * q];
					mju_normalize4(temp0);
					for (int y = 0; y < 4; y++) dx_input[j][y + 4 * q] = temp0[y] - state_nominal[j][y + 4 * q];
				}
			}
		}
		else if (modelid == 12) {
			for (int j = 0; j < stepnum; j++) {
				for (int y = 0; y < 4; y++) temp0[y] = 0.0001 * randGauss(0, 1) + state_nominal[j][y];
				mju_normalize4(temp0);
				for (int y = 0; y < 4; y++) dx_input[j][y] = temp0[y] - state_nominal[j][y];
			}
		}

		// result from the real system
		for (int step_index = 0; step_index < stepnum; step_index++)
		{
			mju_add(d->qpos, dx_input[step_index], state_nominal[step_index], dof + quatnum);
			mju_add(d->qvel, &dx_input[step_index][dof + quatnum], &state_nominal[step_index][dof + quatnum], dof);
			mju_add(d->ctrl, &dx_input[step_index][2*dof + quatnum], &ctrl_nominal[step_index * actuatornum], m->nu);

			// set values for dependent states
			if (modelid == 4) {
				d->qpos[2] = -d->qpos[1];
				d->qpos[3] = d->qpos[1];
				d->qvel[2] = -d->qvel[1];
				d->qvel[3] = d->qvel[1];
			}
			if (modelid == 9) {
				d->qpos[14] = d->qpos[0] + d->qpos[1];
				d->qpos[15] = -d->qpos[1];
				d->qpos[16] = d->qpos[1] + d->qpos[3] + d->qpos[4];
				d->qpos[17] = -d->qpos[4];
				d->qpos[18] = d->qpos[4] + d->qpos[6] + d->qpos[7];
				d->qpos[19] = -d->qpos[7];
				d->qpos[20] = d->qpos[7] + d->qpos[9] + d->qpos[10];
				d->qpos[21] = -d->qpos[10];

				d->qvel[14] = d->qvel[0] + d->qvel[1];
				d->qvel[15] = -d->qvel[1];
				d->qvel[16] = d->qvel[1] + d->qvel[3] + d->qvel[4];
				d->qvel[17] = -d->qvel[4];
				d->qvel[18] = d->qvel[4] + d->qvel[6] + d->qvel[7];
				d->qvel[19] = -d->qvel[7];
				d->qvel[20] = d->qvel[7] + d->qvel[9] + d->qvel[10];
				d->qvel[21] = -d->qvel[10];
			}
			else if (modelid == 11) {
				for (int r = 0; r < 30; r++)
				{
					for (int y = 0; y < 4; y++) d->qpos[y] = state_nominal[step_index][y] + dx_input[step_index][y];
					for (int y = 4; y < 8; y++) d->qpos[y + 4] = state_nominal[step_index][y] + dx_input[step_index][y];
					for (int y = 8; y < 12; y++) d->qpos[y + 8] = state_nominal[step_index][y] + dx_input[step_index][y];
					for (int y = 0; y < 3; y++) d->qvel[y] = state_nominal[step_index][y + dof + quatnum] + dx_input[step_index][y + dof + quatnum];
					for (int y = 3; y < 6; y++) d->qvel[y + 3] = state_nominal[step_index][y + dof + quatnum] + dx_input[step_index][y + dof + quatnum];
					for (int y = 6; y < 9; y++) d->qvel[y + 6] = state_nominal[step_index][y + dof + quatnum] + dx_input[step_index][y + dof + quatnum];
					for (int y = 0; y < actuatornum; y++) d->ctrl[y] = 0;
					mj_step(m, d);
				}
				mju_add(d->ctrl, &dx_input[step_index][2 * dof + quatnum], &ctrl_nominal[step_index * actuatornum], m->nu);
			}

			for (int k = 0; k < integration_per_step; k++) mj_step(m, d);

			mju_sub(dx_simulate[step_index], d->qpos, state_nominal[step_index + 1], dof + quatnum);
			mju_sub(&dx_simulate[step_index][dof + quatnum], d->qvel, &state_nominal[step_index + 1][dof + quatnum], dof);

			if (modelid == 11) {
				for (int y = 0; y < 4; y++) dx_simulate[step_index][y] = d->qpos[y] - state_nominal[step_index + 1][y];
				for (int y = 4; y < 8; y++) dx_simulate[step_index][y] = d->qpos[y + 4] - state_nominal[step_index + 1][y];
				for (int y = 8; y < 12; y++) dx_simulate[step_index][y] = d->qpos[y + 8] - state_nominal[step_index + 1][y];
				for (int y = 0; y < 3; y++) dx_simulate[step_index][y + dof + quatnum] = d->qvel[y] - state_nominal[step_index + 1][y + dof + quatnum];
				for (int y = 3; y < 6; y++) dx_simulate[step_index][y + dof + quatnum] = d->qvel[y + 3] - state_nominal[step_index + 1][y + dof + quatnum];
				for (int y = 6; y < 9; y++) dx_simulate[step_index][y + dof + quatnum] = d->qvel[y + 6] - state_nominal[step_index + 1][y + dof + quatnum];
			}
		}

		for (int j = 0; j < stepnum; j++) {
			//dx_input[j][5] = 0;
			mju_mulMatVec(dx_estimate[j], *matAB_check[j], dx_input[j], kMaxState, kMaxState + kMaxState);
		}

		for (int y = 0; y < 2*dof + quatnum; y++)
		{
			for (int h = 0; h < stepnum; h++)
			{
				if (dx_simulate[h][y] != 0) sysiderr += fabs((dx_estimate[h][y] - dx_simulate[h][y]) / dx_simulate[h][y]);
			}
		}
	}
	sysiderr = sysiderr / (1.0*kTestNum*stepnum*(2*dof + quatnum));
}

// thread function
void sysid(int id, int nroll, int nthd)
{
	MatrixXd delta_x1(nroll, 2*dof + quatnum + actuatornum);
	MatrixXd delta_x2(2 * dof + quatnum, nroll);
	MatrixXd matAB(2 * dof + quatnum, 2 * dof + quatnum + actuatornum);
	mjtNum temp0[4] = { 0 };
	mjtNum printfraction = 0.2;

	// clear statistics
	contacts[id] = 0;
	constraints[id] = 0;
	srand((unsigned)time(NULL) + id);

	// run and time
	double start = gettm();
	for (int step_index = id*int(stepnum/nthd); step_index < (id+1)*int(stepnum / nthd); step_index++)
	{
		for (int rollout_index = 0; rollout_index < nroll; rollout_index++)
		{
			for (int y = 0; y < 2 * dof + quatnum + actuatornum; y++) delta_x1(rollout_index, y) = perturb_coefficient_sysid * ctrl_max * randGauss(0, 1);

			// special process for quaternions
			if (modelid == 10) {
				for (int y = 0; y < 4; y++) temp0[y] = ctrl_max * perturb_coefficient_sysid * randGauss(0, 1) + state_nominal[step_index][y + 3];
				mju_normalize4(temp0);
				for (int y = 0; y < 4; y++) delta_x1(rollout_index, y + 3) = temp0[y] - state_nominal[step_index][y + 3];
			}
			else if (modelid == 11) {
				for (int q = 0; q < quatnum; q++) {
					for (int y = 0; y < 4; y++) temp0[y] = perturb_coefficient_sysid * ctrl_max * randGauss(0, 1) + state_nominal[step_index][y + 4 * q];
					mju_normalize4(temp0);
					for (int y = 0; y < 4; y++) delta_x1(rollout_index, y + 4 * q) = temp0[y] - state_nominal[step_index][y + 4 * q];
				}
			}
			else if (modelid == 12) {
				for (int y = 0; y < 4; y++) temp0[y] = ctrl_max * perturb_coefficient_sysid * randGauss(0, 1) + state_nominal[step_index][y];
				mju_normalize4(temp0);
				for (int y = 0; y < 4; y++) delta_x1(rollout_index, y) = temp0[y] - state_nominal[step_index][y];
			}

			// plus
			for (int y = 0; y < dof + quatnum; y++) d[id]->qpos[y] = state_nominal[step_index][y] + delta_x1(rollout_index, y);
			for (int y = 0; y < dof; y++) d[id]->qvel[y] = state_nominal[step_index][y + dof + quatnum] + delta_x1(rollout_index, y + dof + quatnum);
			for (int y = 0; y < actuatornum; y++) d[id]->ctrl[y] = ctrl_nominal[step_index * actuatornum + y] + delta_x1(rollout_index, 2*dof + quatnum + y);

			// set values for dependent states
			if (modelid == 4) {
				d[id]->qpos[2] = -d[id]->qpos[1];
				d[id]->qpos[3] = d[id]->qpos[1];
				d[id]->qvel[2] = -d[id]->qvel[1];
				d[id]->qvel[3] = d[id]->qvel[1];
			}
			else if (modelid == 9) {
				d[id]->qpos[14] = d[id]->qpos[0] + d[id]->qpos[1];
				d[id]->qpos[15] = -d[id]->qpos[1];
				d[id]->qpos[16] = d[id]->qpos[1] + d[id]->qpos[3] + d[id]->qpos[4];
				d[id]->qpos[17] = -d[id]->qpos[4];
				d[id]->qpos[18] = d[id]->qpos[4] + d[id]->qpos[6] + d[id]->qpos[7];
				d[id]->qpos[19] = -d[id]->qpos[7];
				d[id]->qpos[20] = d[id]->qpos[7] + d[id]->qpos[9] + d[id]->qpos[10];
				d[id]->qpos[21] = -d[id]->qpos[10];

				d[id]->qvel[14] = d[id]->qvel[0] + d[id]->qvel[1];
				d[id]->qvel[15] = -d[id]->qvel[1];
				d[id]->qvel[16] = d[id]->qvel[1] + d[id]->qvel[3] + d[id]->qvel[4];
				d[id]->qvel[17] = -d[id]->qvel[4];
				d[id]->qvel[18] = d[id]->qvel[4] + d[id]->qvel[6] + d[id]->qvel[7];
				d[id]->qvel[19] = -d[id]->qvel[7];
				d[id]->qvel[20] = d[id]->qvel[7] + d[id]->qvel[9] + d[id]->qvel[10];
				d[id]->qvel[21] = -d[id]->qvel[10];
			}
			else if (modelid == 11) {
				for (int r = 0; r < 30; r++)
				{
					for (int y = 0; y < 4; y++) d[id]->qpos[y] = state_nominal[step_index][y] + delta_x1(rollout_index, y);
					for (int y = 4; y < 8; y++) d[id]->qpos[y + 4] = state_nominal[step_index][y] + delta_x1(rollout_index, y);
					for (int y = 8; y < 12; y++) d[id]->qpos[y + 8] = state_nominal[step_index][y] + delta_x1(rollout_index, y);
					mju_zero(d[id]->qvel, m->nv);
					mju_zero(d[id]->ctrl, m->nu);
					mj_step(m, d[id]);
				}///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				for (int y = 0; y < 3; y++) d[id]->qvel[y] = state_nominal[step_index][y + dof + quatnum] + delta_x1(rollout_index, y + dof + quatnum);
				for (int y = 3; y < 6; y++) d[id]->qvel[y + 3] = state_nominal[step_index][y + dof + quatnum] + delta_x1(rollout_index, y + dof + quatnum);
				for (int y = 6; y < 9; y++) d[id]->qvel[y + 6] = state_nominal[step_index][y + dof + quatnum] + delta_x1(rollout_index, y + dof + quatnum);
				for (int y = 0; y < actuatornum; y++) d[id]->ctrl[y] = ctrl_nominal[step_index * actuatornum + y] + delta_x1(rollout_index, 2 * dof + quatnum + y);
			}

			for (int i = 0; i < integration_per_step; i++) mj_step(m, d[id]);

			for (int y = 0; y < dof + quatnum; y++) delta_x2(y, rollout_index) = d[id]->qpos[y] - state_nominal[step_index + 1][y];
			for (int y = 0; y < dof; y++) delta_x2(y + dof + quatnum, rollout_index) = d[id]->qvel[y] - state_nominal[step_index + 1][y + dof + quatnum];

			if (modelid == 11) {
				for (int y = 0; y < 4; y++) delta_x2(y, rollout_index) = d[id]->qpos[y] - state_nominal[step_index + 1][y];
				for (int y = 4; y < 8; y++) delta_x2(y, rollout_index) = d[id]->qpos[y + 4] - state_nominal[step_index + 1][y];
				for (int y = 8; y < 12; y++) delta_x2(y, rollout_index) = d[id]->qpos[y + 8] - state_nominal[step_index + 1][y];
				for (int y = 0; y < 3; y++) delta_x2(y + dof + quatnum, rollout_index) = d[id]->qvel[y] - state_nominal[step_index + 1][y + dof + quatnum];
				for (int y = 3; y < 6; y++) delta_x2(y + dof + quatnum, rollout_index) = d[id]->qvel[y + 3] - state_nominal[step_index + 1][y + dof + quatnum];
				for (int y = 6; y < 9; y++) delta_x2(y + dof + quatnum, rollout_index) = d[id]->qvel[y + 6] - state_nominal[step_index + 1][y + dof + quatnum];
			}

			//// minus
			//for (int y = 0; y < dof + quatnum; y++) d[id]->qpos[y] = state_nominal[step_index][y] - delta_x1(rollout_index, y);
			//for (int y = 0; y < dof; y++) d[id]->qvel[y] = state_nominal[step_index][y + dof + quatnum] - delta_x1(rollout_index, y + dof + quatnum);
			//for (int y = 0; y < actuatornum; y++) d[id]->ctrl[y] = ctrl_nominal[step_index * actuatornum + y] - delta_x1(rollout_index, 2*dof + quatnum + y);

			//if (modelid == 4) {
			//	d[id]->qpos[2] = -d[id]->qpos[1];
			//	d[id]->qpos[3] = d[id]->qpos[1];
			//	d[id]->qvel[2] = -d[id]->qvel[1];
			//	d[id]->qvel[3] = d[id]->qvel[1];
			//}
			//else if (modelid == 9) {
			//	d[id]->qpos[14] = d[id]->qpos[0] + d[id]->qpos[1];
			//	d[id]->qpos[15] = -d[id]->qpos[1];
			//	d[id]->qpos[16] = d[id]->qpos[1] + d[id]->qpos[3] + d[id]->qpos[4];
			//	d[id]->qpos[17] = -d[id]->qpos[4];
			//	d[id]->qpos[18] = d[id]->qpos[4] + d[id]->qpos[6] + d[id]->qpos[7];
			//	d[id]->qpos[19] = -d[id]->qpos[7];
			//	d[id]->qpos[20] = d[id]->qpos[7] + d[id]->qpos[9] + d[id]->qpos[10];
			//	d[id]->qpos[21] = -d[id]->qpos[10];

			//	d[id]->qvel[14] = d[id]->qvel[0] + d[id]->qvel[1];
			//	d[id]->qvel[15] = -d[id]->qvel[1];
			//	d[id]->qvel[16] = d[id]->qvel[1] + d[id]->qvel[3] + d[id]->qvel[4];
			//	d[id]->qvel[17] = -d[id]->qvel[4];
			//	d[id]->qvel[18] = d[id]->qvel[4] + d[id]->qvel[6] + d[id]->qvel[7];
			//	d[id]->qvel[19] = -d[id]->qvel[7];
			//	d[id]->qvel[20] = d[id]->qvel[7] + d[id]->qvel[9] + d[id]->qvel[10];
			//	d[id]->qvel[21] = -d[id]->qvel[10];
			//}

			//for (int i = 0; i < integration_per_step; i++) mj_step(m, d[id]);

			//for (int y = 0; y < dof + quatnum; y++) delta_x2(y, rollout_index) -= d[id]->qpos[y];
			//for (int y = 0; y < dof; y++) delta_x2(y + dof + quatnum, rollout_index) -= d[id]->qvel[y];
		}
		matAB = (delta_x2*delta_x1*((delta_x1.transpose()*delta_x1).inverse())) / 1;
		for (int h = 0; h < 2*dof + quatnum; h++) for (int d = 0; d < 2*dof + quatnum + actuatornum; d++) matAB_check[step_index][h][d] = matAB(h, d);

		// print '.' every printfraction of nrollout for thread 0
		if (id == 0 && step_index >= stepnum / nthd * printfraction)
		{
			printf(".");
			printfraction += 0.2;
		}
		// accumulate statistics
		contacts[id] += d[id]->ncon;
		constraints[id] += d[id]->nefc;
		simtime[id] = gettm() - start;
	}
}

// main function
int main(int argc, const char** argv)
{
    // print help if arguments are missing
    if( argc<3 || argc>9 )
        return finish("\n Usage:  sysid3d modelfile control_timestep stepnum noiselevel rolloutnumber [modeltype [nthread [profile]]]\n");

    // activate MuJoCo Pro license (this must be *your* activation key)
	DWORD usernamesize = 30;
	GetUserName(username, &usernamesize);
	if (username[0] == 'R') {
		strcpy(keyfilename, keyfilepre);
		strcat(keyfilename, "mjkeybig.txt");
		mj_activate(keyfilename);
	}
	else {
		strcpy(keyfilename, keyfilepre);
		strcat(keyfilename, "mjkeysmall.txt");
		mj_activate(keyfilename);
	}

	// get filename, determine file type
	std::string filename(argv[1]);
	bool binary = (filename.find(".mjb") != std::string::npos);
	strcpy(modelfilename, argv[1]);
	strncpy(modelname, modelfilename, strlen(modelfilename) - 4);
	modelSelection(modelname);

	// set timestep and stepnum
	if (sscanf(argv[2], "%lf", &control_timestep) != 1 || control_timestep <= 0)
		return finish("Invalid control_timestep argument");
	if (sscanf(argv[3], "%d", &stepnum) != 1 || stepnum <= 0)
		return finish("Invalid stepnum argument");

	// read nrollout and nthread
	int nrollout = 0, nthread = 0, profile = 0;
	if (sscanf(argv[4], "%lf", &perturb_coefficient_sysid) != 1 || perturb_coefficient_sysid < 0)
		return finish("Invalid noise level argument");
	if (sscanf(argv[5], "%d", &nrollout) != 1 || nrollout <= 0)
		return finish("Invalid nrollout argument");
	if (argc > 6 && modelSelection(argv[6]) != 1) {
		if (sscanf(argv[6], "%d", &nthread) != 1)
			return finish("Invalid nthread argument");
		if (argc > 7)
			if (sscanf(argv[7], "%d", &profile) != 1)
				return finish("Invalid profile argument");
	}
	else if (argc > 7) {
		if (sscanf(argv[7], "%d", &nthread) != 1)
			return finish("Invalid nthread argument");
		if (argc > 8)
			if (sscanf(argv[8], "%d", &profile) != 1)
				return finish("Invalid profile argument");
	}

    // clamp nthread to [1, kMaxThread]
    nthread = mjMAX(1, mjMIN(kMaxThread, nthread));

    // load model
    char error[500] = "Could not load binary model";
    if( binary )
        m = mj_loadModel(modelfilename, 0);
    else
        m = mj_loadXML(modelfilename, 0, error, 500);
    if( !m )
        return finish(error);

	// check timestep setting
	simulation_timestep = m->opt.timestep;
	integration_per_step = (int)(control_timestep / simulation_timestep);
	if (integration_per_step <= 0)
		return finish("Invalid timestep setting");

    // make per-thread data
    int testkey = mj_name2id(m, mjOBJ_KEY, "test");
    for( int id=0; id<nthread; id++ )
    {
        d[id] = mj_makeData(m);
        if( !d[id] )
            return finish("Could not allocate mjData", m);

        // init to keyframe "test" if present
        if( testkey>=0 )
        {
            mju_copy(d[id]->qpos, m->key_qpos + testkey*m->nq, m->nq);
            mju_copy(d[id]->qvel, m->key_qvel + testkey*m->nv, m->nv);
            mju_copy(d[id]->act,  m->key_act  + testkey*m->na, m->na);
        }
    }

	// read nominal control values
	strcpy(datafilename, "result0.txt");
	if ((filestream3 = fopen(datafilename, "r")) != NULL) {
		for (int i = 0; i < actuatornum * stepnum; i++)
		{
			fscanf(filestream3, "%s", data_buff);
			ctrl_nominal[i] = atof(data_buff);
			if (fabs(ctrl_nominal[i]) > ctrl_max) ctrl_max = fabs(ctrl_nominal[i]);
		}
		fclose(filestream3);
	}
	else printf("Could not open file: result.txt\n");

    // install timer callback for profiling if requested
    tm_start = chrono::system_clock::now();
    if( profile )
        mjcb_time = gettm;

    // print start
	if (nthread > 1)
		printf("\nRunning %d rollouts per thread at dt_c = %g, dt_s = %g for %d threads\n\n", int(nrollout * 2 / nthread), control_timestep, m->opt.timestep, nthread);
	else
		printf("\nRunning %d rollouts at dt_c = %g, dt_s = %g\n\n", nrollout * 2, control_timestep, m->opt.timestep);

    // run simulation, record total time
    thread th[kMaxThread];
    double starttime = gettm();
	stateNominal(m, d[0]);

	for (int id = 0; id < nthread; id++) {
		th[id] = thread(sysid, id, nrollout, nthread);
	}
	for (int id = 0; id < nthread; id++)
		th[id].join();
    double tottime = gettm() - starttime;
	sysidCheck(m, d[0]);

    // all-thread summary
    if( nthread>1 )
    {
        printf("Summary for all %d threads\n\n", nthread);
        printf(" Total simulation time  : %.2f s\n", tottime);
        printf(" Total steps per second : %.0f\n", nthread*nrollout*2*stepnum*integration_per_step /tottime);
        printf(" Total realtime factor  : %.2f x\n", nthread*nrollout*2*stepnum*integration_per_step*m->opt.timestep/tottime);
        printf(" Total time per step    : %.4f ms\n\n", 1000*tottime/(nthread*nrollout*2*stepnum*integration_per_step));
        printf("Details for thread 0\n\n");
    }

    // details for thread 0
    printf("\n Simulation time      : %.2f s\n", simtime[0]);
	printf(" Number of steps      : %d\n", nrollout*2*stepnum*integration_per_step);
	printf(" Steps per second     : %.0f\n", nrollout*2*stepnum*integration_per_step / simtime[0]);
	printf(" Realtime factor      : %.2f x\n", nrollout*2*stepnum*integration_per_step*m->opt.timestep / simtime[0]);
	printf(" Time per step        : %.4f ms\n\n", 1000 * simtime[0] / (nrollout*2*stepnum*integration_per_step));
	printf(" Contacts per step    : %d\n", contacts[0] / (nrollout*2*stepnum*integration_per_step));
	printf(" Constraints per step : %d\n", constraints[0] / (nrollout*2*stepnum*integration_per_step));
    printf(" Degrees of freedom   : %d\n\n", m->nv);

    // profiler results for thread 0
    if( profile )
    {
        printf(" Profiler phase (ms per step)\n");
        mjtNum tstep = d[0]->timer[mjTIMER_STEP].duration/d[0]->timer[mjTIMER_STEP].number;
        for( int i=0; i<mjNTIMER; i++ )
            if( d[0]->timer[i].number>0 )
            {
                mjtNum istep = d[0]->timer[i].duration/d[0]->timer[i].number;
                printf(" %16s : %.5f  (%6.2f %%)\n", mjTIMERSTRING[i],
                    1000*istep, 100*istep/tstep);
            }
    }

	// save result to file
	strcpy(datafilename, "lnr.txt");
	if ((filestream3 = fopen(datafilename, "wt+")) != NULL)
	{
		for (int i = 0; i < stepnum; i++)
		{
			for (int h = 0; h < 2*dof + quatnum; h++)
			{
				for (int d = 0; d < 2*dof + quatnum + actuatornum; d++)
				{
					sprintf(data_buff, "%4.8f", matAB_check[i][h][d]);
					fwrite(data_buff, 10, 1, filestream3);
					fputs(" ", filestream3);
				}
				fputs("\n", filestream3);
			}
			fputs("\n", filestream3);
		}
		fputs("sysiderr: ", filestream3);
		sprintf(data_buff, "%2.4f\n", sysiderr);
		fwrite(data_buff, 6, 1, filestream3);
		fputs("\nptb_coef: ", filestream3);
		sprintf(data_buff, "%2.4f\n", perturb_coefficient_sysid);
		fwrite(data_buff, 6, 1, filestream3);
		fclose(filestream3);
	}
	else printf("Could not open file: lnr.txt\n");
	
    // free per-thread data
    for( int id=0; id<nthread; id++ )
        mj_deleteData(d[id]);

    // finalize
	return finish(0, m);
}
