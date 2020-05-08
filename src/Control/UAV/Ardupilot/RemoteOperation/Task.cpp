//***************************************************************************
// Copyright 2007-2020 Universidade do Porto - Faculdade de Engenharia      *
// Laboratório de Sistemas e Tecnologia Subaquática (LSTS)                  *
//***************************************************************************
// This file is part of DUNE: Unified Navigation Environment.               *
//                                                                          *
// Commercial Licence Usage                                                 *
// Licencees holding valid commercial DUNE licences may use this file in    *
// accordance with the commercial licence agreement provided with the       *
// Software or, alternatively, in accordance with the terms contained in a  *
// written agreement between you and Faculdade de Engenharia da             *
// Universidade do Porto. For licensing terms, conditions, and further      *
// information contact lsts@fe.up.pt.                                       *
//                                                                          *
// Modified European Union Public Licence - EUPL v.1.1 Usage                *
// Alternatively, this file may be used under the terms of the Modified     *
// EUPL, Version 1.1 only (the "Licence"), appearing in the file LICENCE.md *
// included in the packaging of this file. You may not use this work        *
// except in compliance with the Licence. Unless required by applicable     *
// law or agreed to in writing, software distributed under the Licence is   *
// distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF     *
// ANY KIND, either express or implied. See the Licence for the specific    *
// language governing permissions and limitations at                        *
// https://github.com/LSTS/dune/blob/master/LICENCE.md and                  *
// http://ec.europa.eu/idabc/eupl.html.                                     *
//***************************************************************************
// Author: Keila Lima                                                       *
//***************************************************************************

// ISO C++ 98 headers.
#include <sstream>
#include <iomanip>

// DUNE headers.
#include <DUNE/DUNE.hpp>
#include <DUNE/Utils/MAVLink.hpp>

// MAVLink headers.
#include <mavlink/ardupilotmega/mavlink.h>

namespace Control
{
  namespace UAV
  {
    namespace Ardupilot
    {
      namespace RemoteOperation
      {
        using DUNE_NAMESPACES;

        const float PWM_MAX  = 1900.0;
        const float PWM_MIN  = 1100.0;
        const float PWM_IDLE = 1500.0;
        const float GAIN_MAX = 1.0; //percentage
        const float GAIN_MIN = 0.1;
        //! Used in roll and pitch
        const float TRIM_MAX  = 200.0;
        const float TRIM_MIN  = -200.0;
        const int TRIM_STEP = 10;
        const uint16_t NOTUSED  = 0; //0xffff;
        const std::string remote_actions[16]={"GainUP","GainDown","TiltUP","TiltDown",
        		"LightDimmer","LightBrighter","PitchForward","PitchBackward","RollLeft","RollRight",
				"Stabilize","DepthHold","Manual","PositionHold","Arm","Disarm"}; //TODO home and SK
        const std::string axis[6] = {"Pitch","Roll","Throttle","Heading","Forward","Lateral"};
        const std::string js_params_id[6] = {"JS_CAM_TILT_STEP","JS_GAIN_MAX","JS_GAIN_MIN",
        		"JS_GAIN_STEPS","JS_LIGHTS_STEPS","JS_THR_GAIN"};
        int rc_pwm[11];
        //! List of ArduPlane modes.
		//! From ArduPlane/defines.h in diydrones git repo.
		enum BUTTONS
		{
		};

		//! see: https://www.ardusub.com/operators-manual/rc-input-and-output.html
		enum RC_INPUT
		{
			Pitch          = 0,
			Roll           = 1,
			Throttle       = 2,
			Heading        = 3,
			Forward        = 4,
			Lateral        = 5,
			Camera_Pan     = 6,
			Camera_Tilt    = 7,
			Lights_1_Level = 8,
			Lights_2_Level = 9,
			Video_Switch   = 10,
		};

        struct Arguments
        {
          //!Gain Step increment and decrement
		  int gain_step;
          Address addr;
          uint16_t port;
          //!ArduSub control channels
          MAVLink::RadioChannel rc [11];
        };

        struct Task: public DUNE::Control::BasicRemoteOperation
        {
          Arguments m_args; // Task arguments.
          Network::UDPSocket* m_socket; //TODO move to Transport/MAVLink
          Network::TCPSocket* m_sender; //TODO move to Transport/MAVLink


          //! Type definition for Arduino packet handler.
			typedef void (Task::* PktHandler)(const mavlink_message_t* msg);
			typedef std::map<int, PktHandler> PktHandlerMap;
			//! Arduino packet handling
			PktHandlerMap m_mlh;

          //! Gains
          float m_gain;
          float m_thr_gain;
          //! Steps - https://github.com/ArduPilot/ardupilot/blob/master/Tools/Frame_params/Sub/bluerov2-3_5.params
          int m_lights_step;
          int m_cam_steps;
          //!Trim values
          float m_pitch_trim;
          float m_roll_trim;
          //! This System ID
          uint8_t m_sysid;
          //! Target_system System ID
          uint8_t m_targetid;
          //! Parsing variables
          uint8_t m_buf[512];
          mavlink_message_t m_recv_msg;
          //! Timer for heartbeat
          Time::Counter<float> m_timer;
          //! MAVLink system status
          int m_sys_status;
          //!Communication status
          bool m_comms;
          //! previous GCS SYSID - before Dune takes control
          int m_gcs;

          Task(const std::string& name, Tasks::Context& ctx):
            DUNE::Control::BasicRemoteOperation(name, ctx),
			m_gain(0.20),
			m_lights_step(100),
			m_cam_steps(50),
			m_pitch_trim(0.0),
			m_roll_trim(0.0),
			m_sysid(254),
            m_targetid(1),
			m_timer(1.0),
			m_sys_status(MAV_STATE_UNINIT),
			m_comms(false),
			m_gcs(1)
          {
            param("Gain Step", m_args.gain_step)
            .minimumValue("2")
            .maximumValue("10")
            .defaultValue("10")
            .units(Units::Percentage)
            .description("Gain Step increment and decrement");

            param("MAVLink ADDR", m_args.addr)
			.defaultValue("127.0.0.1")
			.description("ArduSub Address, can be via MAVProxy");

            param("MAVLink Port", m_args.port)
			.defaultValue("5760")
			.description("ArduSub Port to receive data, can be via MAVProxy");

            param("RC 1 MAX", m_args.rc[RC_INPUT::Pitch].val_max)
			.defaultValue("180")
			.description("Maximum manual control normalized value - associated to the joystick/ccu/accu input.");
            param("RC 1 MIN", m_args.rc[RC_INPUT::Pitch].val_min)
			.defaultValue("-180")
			.description("Minimum manual control normalized value - associated to the joystick/ccu/accu input.");
            param("RC 1 Neutral", m_args.rc[RC_INPUT::Pitch].val_neutral)
			.defaultValue("0")
			.description("Neutral value - associated to the joystick/ccu/accu input.");

            param("RC 2 MAX", m_args.rc[RC_INPUT::Roll].val_max)
			.defaultValue("180")
			.description("Maximum manual control normalized value - associated to the joystick/ccu/accu input.");
			param("RC 2 MIN", m_args.rc[RC_INPUT::Roll].val_min)
			.defaultValue("-180")
			.description("Minimum manual control normalized value - associated to the joystick/ccu/accu input.");
            param("RC 2 Neutral", m_args.rc[RC_INPUT::Roll].val_neutral)
			.defaultValue("0")
			.description("Neutral value - associated to the joystick/ccu/accu input.");


			param("RC 3 MAX", m_args.rc[RC_INPUT::Throttle].val_max)
			.defaultValue("1000")
			.description("Maximum manual control normalized value - associated to the joystick/ccu/accu input.");
			param("RC 3 MIN", m_args.rc[RC_INPUT::Throttle].val_min)
			.defaultValue("-1000")
			.description("Minimum manual control normalized value - associated to the joystick/ccu/accu input.");
            param("RC 3 Neutral", m_args.rc[RC_INPUT::Throttle].val_neutral)
			.defaultValue("0")
			.description("Neutral value - associated to the joystick/ccu/accu input.");


			param("RC 4 MAX", m_args.rc[RC_INPUT::Heading].val_max)
			.defaultValue("180")
			.description("Maximum manual control normalized value - associated to the joystick/ccu/accu input.");
			param("RC 4 MIN", m_args.rc[RC_INPUT::Heading].val_min)
			.defaultValue("-180")
			.description("Minimum manual control normalized value - associated to the joystick/ccu/accu input.");
            param("RC 4 Neutral", m_args.rc[RC_INPUT::Heading].val_neutral)
			.defaultValue("90")
			.description("Neutral value - associated to the joystick/ccu/accu input.");


			param("RC 5 MAX", m_args.rc[RC_INPUT::Forward].val_max)
			.defaultValue("1000")
			.description("Maximum manual control normalized value - associated to the joystick/ccu/accu input.");
			param("RC 5 MIN", m_args.rc[RC_INPUT::Forward].val_min)
			.defaultValue("-1000")
			.description("Minimum manual control normalized value - associated to the joystick/ccu/accu input.");
            param("RC 5 Neutral", m_args.rc[RC_INPUT::Forward].val_neutral)
			.defaultValue("0")
			.description("Neutral value - associated to the joystick/ccu/accu input.");


			param("RC 6 MAX", m_args.rc[RC_INPUT::Lateral].val_max)
			.defaultValue("1000")
			.description("Maximum manual control normalized value - associated to the joystick/ccu/accu input.");
			param("RC 6 MIN", m_args.rc[RC_INPUT::Lateral].val_min)
			.defaultValue("-1000")
			.description("Minimum manual control normalized value - associated to the joystick/ccu/accu input.");
            param("RC 6 Neutral", m_args.rc[RC_INPUT::Lateral].val_neutral)
			.defaultValue("0")
			.description("Neutral value - associated to the joystick/ccu/accu input.");


            // Setup packet handlers
		   // IMPORTANT: set up function to handle each type of MAVLINK packet here
		   m_mlh[MAVLINK_MSG_ID_PARAM_VALUE] = &Task::handleParams;
		   m_mlh[MAVLINK_MSG_ID_SYSTEM_TIME] = &Task::handleSysTime;
		   m_mlh[MAVLINK_MSG_ID_RC_CHANNELS] = &Task::handleRC;

            bind<Teleoperation>(this);
            bind<TeleoperationDone>(this);

            // Add remote actions.
            addActionAxis("Forward"); // X
            addActionAxis("Lateral"); // Y
            addActionAxis("Up"); // Z
            addActionAxis("Heading"); // R ?

            //! JS Buttons (16)
            addActionButton("TiltUP"); // gimbal with mounted camera
            addActionButton("TiltDown");
            addActionButton("Center");
            //addActionButton("InputHold"); //Handled at A(CCU) side
            addActionButton("LightDimmer");
            addActionButton("LightBrighter");
            addActionButton("GainUP");
            addActionButton("GainDown");
            //addActionButton("ArmDisarm"); //TODO instead of teleoperation?
            //! Shift functions and hold input are handled at a higher level in the (A)CCU side
            //! Shitf Buttons
            addActionButton("PitchForward"); //Trim pitch
            addActionButton("PitchBackward");
            addActionButton("RollLeft"); //Trim roll
            addActionButton("RollRight");
            //! APM Modes
            addActionButton("Stabilize");
            addActionButton("DepthHold");
            addActionButton("PositionHold");
            addActionButton("Manual");
            //! Free buttons - A, RT, LT

          }

          void
          onUpdateParameters(void)
          {
          }

          void
		  openConnection(void)
          {
        	try{
				m_socket = new UDPSocket;
				m_sender = new TCPSocket;
				m_sender->bind(5770, Address::Any, true);
				m_sender->connect(m_args.addr, m_args.port);
				m_sender->setNoDelay(true);
				m_socket->bind(14551,Address::Any,true);
				inf(DTR("Ardupilot  Teleoperation interface initialized"));
				m_comms = true;
				requestGCSParam();
				handshake();
			  }
			  catch(std::exception& e){
				m_comms = false;
				Memory::clear(m_socket);
				Memory::clear(m_sender);
				war(DTR("Connection failed: %s"),e.what());
				setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_COM_ERROR);
			  }
          }

          void
		  handshake(void)
          {
        	  debug(DTR("Sending GCS configurations"));
        	  mavlink_message_t msg;
        	  mavlink_msg_param_request_list_pack(m_sysid, 1, &msg, m_targetid, 0);
              uint8_t buf[512];
              int len = mavlink_msg_to_send_buffer(buf, &msg);
              sendData(buf, len);
              setParamByName("FS_GCS_ENABLE",3.0);//Heartbeat lost 0:Disabled; 1:Warn; 2:Disarm; 3:Depth Hold; 4:Surface
          }

		  void
		  onResourceAcquisition(void)
          {
            for(int i=0;i<11;i++)
            {
              m_args.rc[i].pwm_max = PWM_MAX;
              m_args.rc[i].pwm_min = PWM_MIN;
              m_args.rc[i].pwm_neutral = PWM_IDLE;
              m_args.rc[i].reverse = false;

            }
            openConnection();
            m_sys_status = MAV_STATE_BOOT;
          }
		  void
		  onResourceRelease(void)
		  {
			  m_sys_status = MAV_STATE_STANDBY;
			  if(isActive() && isStopping()) {
				  m_sys_status = MAV_STATE_POWEROFF;
				//Disable control
				disableControl();
				Time::Delay::wait(1.0);
			  }
			  Memory::clear(m_socket);
			  Memory::clear(m_sender);
		  }

          void
          onDeactivation(void)
          {
	        m_sys_status = MAV_STATE_STANDBY;
            disableControl();
            war("Deactivating Ardupilot control");
          }

          void
          onConnectionTimeout(void)
          {
        	  //TODO
          }

          //Disabling GCS control from dune to stop expecting heartbeat msgs
          void
		  disableControl(void)
          {
        	//Set neutral control
        	debug(DTR("Disabling GCS control"));
         	idle();
            mavlink_message_t msg;
  			uint8_t buf[512];
  			mavlink_msg_change_operator_control_pack(m_sysid, 1, &msg, m_targetid,
  					1, //0: request control of this MAV, 1: Release control of this MAV
					0, 0);

  			int len = mavlink_msg_to_send_buffer(buf, &msg);
  			sendData(buf, len);
			setParamByName("SYSID_MYGCS",m_gcs); // Reestablish old GCS control before Dune
          }

          void
		  consume(const IMC::Teleoperation* m)
          {
	          m_sys_status = MAV_STATE_ACTIVE;
	          setParamByName("SYSID_MYGCS",m_sysid);
	          mavlink_message_t msg;
	          uint8_t buf[512];
              mavlink_msg_change_operator_control_pack(m_sysid, 1, &msg, m_targetid,
            		  0, //0: request control of this MAV, 1: Release control of this MAV
            		  0, 0);
              int len = mavlink_msg_to_send_buffer(buf, &msg);
              sendData(buf, len);
			  requestParams();
			  changeMode(MAVLink::SUB_MODE_MANUAL);
			  arm();
			  idle();
			  inf(DTR("Gain is at %f percent"),(m_gain*100));
			  war(DTR("Started Teleoperation requested by: %s"), m->custom.c_str()); //FIXME check src? and resolve id
			  //Control Loops
			  enableControlLoops(IMC::CL_YAW_RATE | IMC::CL_PITCH | IMC::CL_ROLL| IMC::CL_DEPTH| IMC::CL_THROTTLE);
          }

          void
		  consume(const IMC::TeleoperationDone* msg)
          {
	         m_sys_status = MAV_STATE_STANDBY;
        	 disableControl();
          }

          bool
		  isReversibleAxis(int channel)
          {
			if (channel == RC_INPUT::Forward || channel == RC_INPUT::Lateral
					|| channel == RC_INPUT::Throttle || channel == RC_INPUT::Heading)
				return true;
			return false;
          }

          void
          idle()
          {
            for(int channel=0; channel < 11; channel++)
            {
            	rc_pwm[channel] = PWM_IDLE;
            }
               // Clear pitch/roll trim settings //TODO
//               pitchTrim = 0;
//               rollTrim  = 0;
            actuate();
          }

          //! Send commands to ArduSub
          void
          actuate(void)
          {
            mavlink_message_t msg;
            mavlink_msg_rc_channels_override_pack(m_sysid,1,&msg,m_targetid,0,rc_pwm[0],rc_pwm[1],rc_pwm[2],rc_pwm[3],rc_pwm[4],rc_pwm[5],rc_pwm[6],rc_pwm[7]);
            uint8_t buf[512];
            int len = mavlink_msg_to_send_buffer(buf, &msg);
            sendData(buf, len);
//            for(int i=0;i<11;i++)
//            	debug(DTR("Actuating on channel %d with PWM: %d"),i+1,rc_pwm[i]); //TODO clean-up
          }

          bool
          disarm(void)
          {
            try {
              uint8_t buffer[512];
              //sendCommandPacket(MAV_CMD_COMPONENT_ARM_DISARM, 0);
              uint16_t size = MAVLink::packCmd2Buffer(MAV_CMD_COMPONENT_ARM_DISARM,m_targetid,buffer,0);
              sendData(buffer,size);
              return true;
            }
            catch(std::exception& e) {
              war(DTR("Error disarming: %s"),e.what());
              return false;
            }
          }

          bool
          arm(void)
          {
            try
            {
              uint8_t buffer[512];
              uint16_t size = MAVLink::packCmd2Buffer(MAV_CMD_COMPONENT_ARM_DISARM,m_targetid,buffer,1);
              sendData(buffer,size);
              trace(DTR("Sent Arm Command."));
              return true;
            }
            catch (std::exception& e)
            {
              war(DTR("Error arming: %s"),e.what());
              return false;
            }
          }

          void
		  sendHeartbeat(void)
          {
        	uint8_t buffer[512];
        	mavlink_message_t msg;
        	mavlink_msg_heartbeat_pack(m_sysid, 1, &msg, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, m_sys_status);
			uint16_t size = mavlink_msg_to_send_buffer(buffer, &msg);
			sendData(buffer,size);
			trace(DTR("Sent Heatbeat."));
          }

          void
		  changeMode(uint8_t mode)
          {
        	  uint8_t buf[512];
			  mavlink_message_t msg;

			  mavlink_msg_set_mode_pack(m_sysid, 1, &msg,
										m_targetid,
										1,
										mode);

			  uint16_t n = mavlink_msg_to_send_buffer(buf, &msg);
			  sendData(buf, n);
			  debug("Set mode to %d",mode);
          }

      	void
      	requestParams()
      	{
      		uint8_t buf[512];
      		mavlink_message_t msg;
      		uint16_t n;
      		char param_id[16];
      		for(int js_param=0;js_param<6;js_param++)
      		{
      			std::strcpy(param_id, js_params_id[js_param].c_str());
				mavlink_msg_param_request_read_pack(m_sysid, 1, &msg, m_targetid, 0,
						param_id, -1);
				n = mavlink_msg_to_send_buffer(buf, &msg);
				sendData(buf, n);
				inf(DTR("Requesting parameter: %s"),param_id);
      		}
      		std::strcpy(param_id, "SYSID_MYGCS");
      		mavlink_msg_param_request_read_pack(m_sysid, 1, &msg, m_targetid, 0,
      								param_id, -1);
			n = mavlink_msg_to_send_buffer(buf, &msg);
			sendData(buf, n);
      	}

      	void
		requestGCSParam(void)
      	{
      		uint8_t buf[512];
			mavlink_message_t msg;
			uint16_t n;
			char param_id[16];
      		std::strcpy(param_id, "SYSID_MYGCS");
			mavlink_msg_param_request_read_pack(m_sysid, 1, &msg, m_targetid, 0,
									param_id, -1);
			n = mavlink_msg_to_send_buffer(buf, &msg);
			sendData(buf, n);
      	}

      	void
      	setParamByName(std::string param_id, float value)
      	{
			mavlink_message_t msg;
			mavlink_msg_param_set_pack(255, 0, &msg,
					m_targetid, //! target_system System ID
					1, //! target_component Component ID
					param_id.c_str(), //! Parameter name
					value, //! MAV GPS Type
					MAV_PARAM_TYPE_UINT8); //! Parameter type //FIXME check type
			uint8_t buf[512];
			int n = mavlink_msg_to_send_buffer(buf, &msg);
			sendData(buf, n);
			inf(DTR("Setting parameter: %s %f"),param_id.c_str(),value);
      	}

      	/*
      	 * Save some GCS and Joystick related parameters for control
      	 * and PWM calculation
      	 * https://www.ardusub.com/operators-manual/full-parameter-list.html
      	 */
          void
		  handleParams(const mavlink_message_t* msg)
		  {
			mavlink_param_value_t parameter;
			mavlink_msg_param_value_decode(msg, &parameter);
			debug(DTR("Received Parameter: %s with value %f"),parameter.param_id,parameter.param_value);
			if(std::strcmp(js_params_id[5].c_str(),parameter.param_id) == 0){
				m_thr_gain = parameter.param_value; //save Throttle gain
			}
			else if(std::strcmp(js_params_id[4].c_str(),parameter.param_id)==0){
				m_lights_step = parameter.param_value; //save JS_Lights_Step gain
			}
			else if(std::strcmp(js_params_id[0].c_str(),parameter.param_id)==0){
				m_cam_steps = parameter.param_value; //save JS_CAM_TILT_STEP gain
			}
			else if(std::strcmp(js_params_id[3].c_str(),parameter.param_id)==0
					&& m_args.gain_step != parameter.param_value){
				(parameter.param_id, (float) m_args.gain_step);
			}
			else if(std::strcmp("SYSID_MYGCS",parameter.param_id)==0
					&& (float) m_gcs != parameter.param_value && (float) m_sysid != parameter.param_value){
				debug("Updating GCS from %f to %f",m_gcs,parameter.param_value);
				m_gcs = (int) parameter.param_value;
				if(isActive())
					war(DTR("Ardupilot Ground Control Station is not DUNE"));
			}
		  }

          void
		  handleSysTime(const mavlink_message_t* msg)
          {
        	  mavlink_system_time_t sysTime;
        	  mavlink_msg_system_time_decode(msg, &sysTime);
//        	  sysTime.time_boot_ms;
          }

          void
		  handleRC(const mavlink_message_t* msg)
          {
        	  mavlink_rc_channels_t channels;
        	  mavlink_msg_rc_channels_decode(msg, &channels);
              trace(DTR("RC Channel 1 PWM %d"),channels.chan1_raw);
              trace(DTR("RC Channel 2 PWM %d"),channels.chan2_raw);
              trace(DTR("RC Channel 3 PWM %d"),channels.chan3_raw);
              trace(DTR("RC Channel 4 PWM %d"),channels.chan4_raw);
              trace(DTR("RC Channel 5 PWM %d"),channels.chan5_raw);
              trace(DTR("RC Channel 6 PWM %d"),channels.chan6_raw);
              trace(DTR("RC Channel 7 PWM %d"),channels.chan7_raw);
              trace(DTR("RC Channel 8 PWM %d"),channels.chan8_raw);
              trace(DTR("RC Channel 9 PWM %d"),channels.chan9_raw);
              trace(DTR("RC Channel 10 PWM %d"),channels.chan10_raw);
              trace(DTR("RC Channel 11 PWM %d"),channels.chan11_raw);
          }

          int
          sendData(uint8_t* buf, int len) //TODO move to Transport/MAVLink
          {
        	  if(!m_comms)
        		  return 0;
        	trace(DTR("Sending MAVLINK Message"));
            int res = 0;
			try {
			  res = m_sender->write((char*)buf, len);
			  trace(DTR("Sent %d bytes of %d via UDP: %s %d"),res,len,m_args.addr.c_str(),m_args.port);
			  m_sender->flushOutput();
			}
			catch (std::exception& e)
			{
			  err(DTR("Unable to send data to MAVLink System: %s"),e.what());
			  openConnection();
			}
			return res;
          }

          void
		  handleData(int n)
          {
        	  mavlink_status_t status;
              for (int i = 0; i < n; i++)
              {
                int rv = mavlink_parse_char(MAVLINK_COMM_0, m_buf[i], &m_recv_msg, &status);
                if (status.packet_rx_drop_count)
                	return;
			if(rv)
			{
			  PktHandler h = m_mlh[m_recv_msg.msgid];

			  if (!h)
				continue;  // Ignore this packet (no handler for it)

			  // Call handler
			  (this->*h)(&m_recv_msg);
			  //m_targetid = m_recv_msg.sysid;

			}
		  }
          }

          //! Verifies the existence of actions for each axis and button
          //! Converts actions tuples in pwm values and applies idle values to the rest of channels
          void
          onRemoteActions(const IMC::RemoteActions* msg)
          {
        	//mavlink_msg_manual_control_pack(system_id, component_id, msg, target, x, y, z, r, buttons);
//        	war(DTR("Processing RemoteActions: %s"),msg->actions.c_str());
            TupleList tl(msg->actions);
            int button;
			button = tl.get("GainUP", 0);
			if( button == 1 )
			{
				m_gain+= (float) m_args.gain_step/100;
				m_gain = std::min(m_gain,GAIN_MAX);
				war(DTR("Gain is at %f percent"),(m_gain*100));
			}
			else
			{
				button = tl.get("GainDown", 0);
				if( button == 1)
				{
					m_gain-= (float) m_args.gain_step/100;
					m_gain = std::max(m_gain,GAIN_MIN);
					war(DTR("Gain is at %f percent"),(m_gain*100));
				}

			}

			for(int channel=0;channel<6;channel++)
			{
				float value = tl.get(axis[channel], NAN);
//				war(DTR("Value for %s on channel %f: "),axis[channel].c_str(),channel);
				if( !isNaN(value))
				{
					value = value * m_gain; //Apply gain
					if(isReversibleAxis(channel)){
						m_args.rc[channel].reverse = false;
						rc_pwm[channel]  = MAVLink::mapRC2PWM(&m_args.rc[channel], value);
//						war(DTR("Value from channel %s (%d):  %f"),axis[channel].c_str(),channel,value);
					}
					else
					{
						if(value <= m_args.rc[channel].val_neutral)
							m_args.rc[channel].reverse = true;
						else
							m_args.rc[channel].reverse = false;
						rc_pwm[channel] = MAVLink::mapRC2PWM(&m_args.rc[channel], value);
					}
				}
				else {
//					war("NEUTRAL CONTROL");
					//reset channel to neutral control
					m_args.rc[channel].reverse = false;
					rc_pwm[channel] = PWM_IDLE;
				}
//				war(DTR("CHANNEL %s with value: %f"),axis[channel].c_str(),rc_pwm[channel]);
			}

            //! Deal with buttons actions 1/0's
			button = tl.get("TiltUP", 0);
			if( button == 1)
			{
				float newV = rc_pwm[RC_INPUT::Camera_Tilt] + m_cam_steps;
				newV = std::min(newV,(float)PWM_MAX);
				rc_pwm[RC_INPUT::Camera_Tilt] = newV;

			}
			else {
				button = tl.get("TiltDown", 0);
				if(button == 1)
				{
					float newV = rc_pwm[RC_INPUT::Camera_Tilt] - m_cam_steps;
					newV = std::max(newV,(float)PWM_MIN);
					rc_pwm[RC_INPUT::Camera_Tilt] = newV;
				}
				else {
					button = tl.get("Center", 0);
					if( button == 1) {
						rc_pwm[RC_INPUT::Camera_Tilt] = PWM_IDLE;
					}
				}
			}

			//Handle Lights
			button = tl.get("LightBrighter", 0);
			if( button == 1)
			{
				float newV = rc_pwm[RC_INPUT::Lights_1_Level] + m_lights_step;
				newV = std::min(newV,(float)PWM_MAX);
				rc_pwm[RC_INPUT::Lights_1_Level] = newV;
				rc_pwm[RC_INPUT::Lights_2_Level] = newV;  //Same command for both lights
			}
			else {
				button = tl.get("LightDimmer", 0);
				if( button == 1)
				{
					float newV = rc_pwm[RC_INPUT::Lights_1_Level] - m_lights_step;
					newV = std::max(newV,(float)PWM_MIN);
					rc_pwm[RC_INPUT::Lights_1_Level] = newV;
					rc_pwm[RC_INPUT::Lights_2_Level] = newV;  //Same comand for both lights
				}
			}

			//Adjust Pitch and Roll - these values don't need to be reset after each iteraction
			// more details in https://www.ardusub.com/operators-manual/button-functions.html
			// and https://github.com/ArduPilot/ardupilot/blob/master/ArduSub/joystick.cpp#L332
			button = tl.get("PitchForward", 0);
			if(button == 1) {
				float newV = m_pitch_trim+TRIM_STEP;
				m_pitch_trim = std::min(newV,TRIM_MAX);
			}

			button = tl.get("PitchBackward", 0);
			if(button == 1) {
				float newV = m_pitch_trim-TRIM_STEP;
				m_pitch_trim = std::max(newV,TRIM_MIN);
			}

			button = tl.get("RollRight", 0);
			if(button == 1) {
				float newV = m_roll_trim+TRIM_STEP;
				m_roll_trim = std::min(newV,TRIM_MAX);
			}

			button = tl.get("RollLeft", 0);
			if(button == 1) {
				float newV = m_roll_trim-TRIM_STEP;
				m_roll_trim = std::max(newV,TRIM_MIN);
			}

			button = tl.get("Stabilize", 0);
			if( button == 1)
			{
				changeMode(MAVLink::SUB_MODE_STABILIZE);

			}
			button = tl.get("DepthHold", 0);

			if( button == 1)
			{
				changeMode(MAVLink::SUB_MODE_DEPTH_HOLD);
			}
			button = tl.get("PositionHold", 0);
			if( button == 1)
			{
				changeMode(MAVLink::SUB_MODE_POS_HOLD);
			}

			button = tl.get("Manual", 0);
			if( button == 1)
			{
				changeMode(MAVLink::SUB_MODE_MANUAL);
			}
			button = tl.get("Disarm", 0);
			if( button == 1)
			{
				disarm();
			}

			button = tl.get("Arm", 0);
			if( button == 1)
			{
				arm();
			}

			actuate();
          }

          int
		  receiveData(uint8_t* buf, size_t blen)
		  {
			  try
			  {
				 if (m_socket){
				   trace(DTR("Received MAVLINK data with size: %d"),blen);
				   return m_socket->read(buf, blen);
				 }
			  }
			  catch (std::exception& e)
			  {
				err(DTR("Error Receiving data: %s"), e.what());
				Memory::clear(m_socket);
				setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_COM_ERROR);
				openConnection();
				return 0;
			  }
			return 0;
		  }

          bool
		  poll(double timeout)
		  {
			if (m_socket != NULL)
			  return Poll::poll(*m_socket, timeout);

			return false;
		  }

          void
          onMain(void)
          {
            while (!stopping())
            {
            	int counter = 0;
            	if(m_socket != NULL){
				  while (poll(0.01) && counter < 100)
				  {
					counter++;

					int n = receiveData(m_buf, sizeof(m_buf));
					if (n < 0)
					{
					  debug("Receive error");
					  break;
					}
					handleData(n);
				  }
				  if(m_timer.overflow()) // 1sec
					  sendHeartbeat();
            	}
            	else {
            		Time::Delay::wait(0.5);
            		openConnection(); //reopen connection
            		m_timer.reset();
            	}
				  // Handle IMC messages from bus
				  consumeMessages();
			  }
          }
      };
    }
  }
}
}

DUNE_TASK
