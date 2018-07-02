#include "Myekf2.h"
#include <fstream>
#include<time.h>
class Ekf2;
std::ifstream read1("data/imu_data.txt");
std::ifstream read2("data/gps_data.txt");
std::ifstream read3("data/mag_data.txt");
std::ifstream read4("data/baro_data.txt");
std::ofstream euler_estimator("euler_estimator.txt");
std::ofstream position_estimator("position_estimator.txt");

bool bReadGPS;


namespace ekf2
{
Ekf2 *instance = nullptr;
}

Ekf2::Ekf2():
	_ekf(),
	_params(_ekf.getParamHandle())
{

}

Ekf2::~Ekf2()
{

}


void Ekf2::print_status()
{
	printf("local position OK %s", (_ekf.local_position_is_valid()) ? "[YES]" : "[NO]");
	printf("global position OK %s", (_ekf.global_position_is_valid()) ? "[YES]" : "[NO]");
}

void Ekf2::task_main()
{
	// initialise parameter cache// TODO
	//updateParams();
//	std::ifstream read1("data/imu_data.txt");

	float gyro_integral_dt = 0;
	float accelerometer_integral_dt = 0;
	float last_IMUtime = 0;
    float now = 0;
    double gps_time_s, lat, lon, alt;

	while (!_task_should_exit && !read1.eof()  && !read2.eof()) {

		bool isa = true;
		bool gps_updated = false;
		bool airspeed_updated = false;
		bool optical_flow_updated = false;
		bool range_finder_updated = false;
		bool vehicle_land_detected_updated = false;
		bool vision_position_updated = false;
		bool vision_attitude_updated = false;
		bool vehicle_status_updated = false;

		// long gyro_integral_dt = 0.01;
		// // in replay mode we are getting the actual timestamp from the sensor topic
			
		read1 >> now;	//ms
		float temp;
		read1 >> temp;	
		now *= 1.e3f;	//us
		//printf("time now: %lf\n", now);
		gyro_integral_dt = now - last_IMUtime;	//us
		gyro_integral_dt /= 1.e6f;	//s
		accelerometer_integral_dt = now - last_IMUtime;
		accelerometer_integral_dt /=1.e6f;

		// // push imu data into estimator
		float gyro_integral[3],gyro_rad[3];
		read1 >> gyro_rad[0];	read1 >> gyro_rad[1];	read1 >> gyro_rad[2];
		//printf("gyro:%lf,%lf,%lf,%lf s\n", gyro_rad[0], gyro_rad[1], gyro_rad[2],gyro_integral_dt);

		gyro_integral[0] = gyro_rad[0] * gyro_integral_dt;
		gyro_integral[1] = gyro_rad[1] * gyro_integral_dt;
		gyro_integral[2] = gyro_rad[2] * gyro_integral_dt;

		float accel_integral[3],accelerometer_m_s2[3];
		read1 >> accelerometer_m_s2[0];	read1 >> accelerometer_m_s2[1];	read1 >> accelerometer_m_s2[2];
		//printf("accelerometer_m_s2:%lf,%lf,%lf\n", accelerometer_m_s2[0], accelerometer_m_s2[1], accelerometer_m_s2[2]);

		
		accel_integral[0] = accelerometer_m_s2[0] * accelerometer_integral_dt;
		accel_integral[1] = accelerometer_m_s2[1] * accelerometer_integral_dt;
		accel_integral[2] = accelerometer_m_s2[2] * accelerometer_integral_dt;
 
		_ekf.setIMUData(now, gyro_integral_dt * 1.e6f, accelerometer_integral_dt * 1.e6f,
				gyro_integral, accel_integral);		

		last_IMUtime = now;

		if(bReadGPS)
		{
			read2 >> gps_time_s;	//ms
			float temp1; read2>>temp1;read2>>temp1;read2>>temp1;read2>>temp1;read2>>temp1;	
			read2 >> lat;
			read2 >> lon;
			read2 >> alt;read2 >> alt;
			read2>>temp1;read2>>temp1;read2>>temp1;read2>>temp1;
			bReadGPS = false;		
		}
		if(gps_time_s * 1.e3f < now)
		{
			gps_updated = true;
			bReadGPS = true;
		}
		if(gps_updated)
		{
			struct gps_message gps_msg = {};
			gps_msg.time_usec = (uint64_t)(gps_time_s * 1.e3f);
			gps_msg.lat = (int32_t)(lat * 1.e7f);
			gps_msg.lon = (int32_t)(lon * 1.e7f);
			gps_msg.alt = (int32_t)(alt * 1.e3f);
			printf("time now: %lf\n", now);
			printf("gps: %ld, %d, %d, %d\n",gps_msg.time_usec,gps_msg.lat,gps_msg.lon,gps_msg.alt);
			gps_msg.fix_type = 3;
			gps_msg.eph = 0;
			gps_msg.epv = 0;
			gps_msg.sacc = 0;
			gps_msg.vel_m_s = 0;
			gps_msg.vel_ned[0] = 0;
			gps_msg.vel_ned[1] = 0;
			gps_msg.vel_ned[2] = 0;
			gps_msg.vel_ned_valid = 0;
			gps_msg.nsats = 8;
			//TODO add gdop to gps topic
			gps_msg.gdop = 0.0f;

			 _ekf.setGpsData(gps_msg.time_usec, &gps_msg);
		}

		//sleep(1);



		//run the EKF update and output
		if (_ekf.update()) {
			printf("zyxloveljs\n");

			matrix::Quaternion<float> q;
			_ekf.copy_quaternion(q.data());

			float velocity[3];
			_ekf.get_velocity(velocity);

			float gyro_rad[3];

			{
				// generate control state data
				float gyro_bias[3] = {};
				_ekf.get_gyro_bias(gyro_bias);
				gyro_rad[0] = gyro_rad[0] - gyro_bias[0];
				gyro_rad[1] = gyro_rad[1] - gyro_bias[1];
				gyro_rad[2] = gyro_rad[2] - gyro_bias[2];

				// Velocity in body frame
				Vector3f v_n(velocity);
				matrix::Dcm<float> R_to_body(q.inversed());
				Vector3f v_b = R_to_body * v_n;


				// Local Position NED
				float position[3];
				_ekf.get_position(position);
				printf("position: %lf,%lf,%lf\n", position[0], position[1], position[2]);
				position_estimator<< now/1.e6f <<" "<<position[0] <<" "<<position[1] <<" "
				<<position[2] <<" "<<std::endl;
				// Attitude quaternion
				//q.copyTo(ctrl_state.q);

				//_ekf.get_quat_reset(&ctrl_state.delta_q_reset[0], &ctrl_state.quat_reset_counter);

				// Acceleration data
				matrix::Vector<float, 3> acceleration(accelerometer_m_s2);

				float accel_bias[3];
				_ekf.get_accel_bias(accel_bias);
				// ctrl_state.x_acc = acceleration(0) - accel_bias[0];
				// ctrl_state.y_acc = acceleration(1) - accel_bias[1];
				// ctrl_state.z_acc = acceleration(2) - accel_bias[2];

				// // compute lowpass filtered horizontal acceleration
				acceleration = R_to_body.transpose() * acceleration;
				// _acc_hor_filt = 0.95f * _acc_hor_filt + 0.05f * sqrtf(acceleration(0) * acceleration(0) +
				// 		acceleration(1) * acceleration(1));
				// ctrl_state.horz_acc_mag = _acc_hor_filt;

				// ctrl_state.airspeed_valid = false;

			}
			
			// generate vehicle local position data

			float pos[3] = {};
			// Position of body origin in local NED frame
			_ekf.get_position(pos);
			//printf("%f  %f  %f\n", pos[0],pos[1],pos[2]);

			// Velocity of body origin in local NED frame (m/s)

			// TODO: better status reporting
	

			// Position of local NED origin in GPS / WGS84 frame
			
			// true if position (x, y) is valid and has valid global reference (ref_lat, ref_lon)
			//_ekf.get_ekf_origin(&lpos.ref_timestamp, &ekf_origin, &lpos.ref_alt);
		
			// The rotation of the tangent plane vs. geographical north
			matrix::Eulerf euler(q);
			printf("euler: %f  %f  %f\n", euler.phi(),euler.theta(),euler.psi());
				euler_estimator<< now/1.e6f <<" "<<euler.phi() <<" "<<euler.theta() <<" "
				<<euler.psi() <<" "<<std::endl;			
			// TODO: uORB definition does not define what these variables are. We have assumed them to be horizontal and vertical 1-std dev accuracy in metres
			Vector3f pos_var, vel_var;
			_ekf.get_pos_var(pos_var);
			_ekf.get_vel_var(vel_var);
	
		
		} 

	}


}
int main(int argc, char *argv[])
{
	printf("asasssa\n" );
	bReadGPS = true;
	Ekf2* _ekf2 = new Ekf2();
	//_ekf2->print_status();
	_ekf2->task_main();


	return 1;
}
