# ShadowMountPlusFan (PS5)

1.Program Logic 
For unknown reasons, a jailbroken PS5 console's Southbridge loses control over the cooling fan's speed. Regardless of how high the APU temperature gets, the fan speed remains stuck at 19%. As a result, the APU temperature can spike up to 90°C in many games. To fix this, homebrew applications must be used to rewrite the fan temperature control thresholds and re-activate the Southbridge's fan control. Examples of such programs include etaHEN and PHU game tools.Furthermore, the PS5 resets its fan temperature control threshold (to 79°C) whenever the console changes its operational state—such as launching a game, quitting a game, returning to the home screen, or entering standby mode. Therefore, a memory-resident program must periodically refresh the fan control values in the Southbridge. Otherwise, the custom fan threshold settings will become invalid as soon as a state transition occurs.Consequently, this modification relies on a memory-resident SMP program to detect the console's operational status in real time. It automatically rewrites the temperature threshold data into the Southbridge fan controller whenever a state change occurs, such as starting or exiting a game.

This modified plugin essentially inserts a piece of code into the original ShadowMountPlus_1.6beta16 source code to configure the Southbridge fan control parameters based on a configuration file. The core code logic is as follows:
 int fan_fd = open("/dev/icc_fan", O_RDWR);   //Access the Southbridge fan device in read/write mode.
        if (fan_fd > 0) {
           uint8_t buf[28] = {0};    // Allocates a 28-byte buffer. For consoles running higher firmware versions (10.xx and above), a 28-byte buffer is mandatory to save fan data.
           buf[5] = target_temp;     // Target temperature is written to Offset 5.   
           ioctl(fan_fd, 0xC01C8F07UL, buf);  //Write temperature data to the fan device controller.
           close(fan_fd);
    }  
    
This code writes the specified temperature thresholds to the Southbridge fan controller by sending the 0xC01C8F07 I/O control code (IOCTL) to its low-level driver. This control logic is identical to that used in other fan control plugins like PHU and etaHEN. Aside from this specific modification, I have not altered any other processing logic in the original ShadowMountPlus_1.6beta16.

2.How to Use
It is recommended to use Payload Manager to load this plugin.
Make sure to set up the configuration file before running this plugin. Modify the Shadowmount plus configuration file located in the /data/shadowmount directory by adding target_temp = 75 to the end of the file. You can change the value after the equals sign to your desired fan temperature threshold, allowing the fan to automatically adjust its speed based on this target temperature.If you do not modify the configuration file, the plugin will default to a threshold of 75°C for fan speed control.The plugin's default safe temperature threshold range is 60°C to 85°C. If the temperature set in the configuration file exceeds this range, the plugin will display a prompt indicating that the temperature setting is out of the allowed range, and will automatically reset the threshold to 75°C.

3.Test Results
ested on a PS5 console running firmware 7.61 with the fan temperature threshold set to 75°C. When the APU temperature exceeds this threshold, the PS5 fan speed automatically accelerates linearly from 19%—the higher the temperature, the higher the fan speed. As the APU temperature cools down, the fan speed gradually drops back to 19%, successfully achieving automatic fan speed adjustment based on the APU temperature.In actual testing, the combination of kstuff lite 1.09 + SMP fan.elf works extremely stably, with rest mode (standby) and wake-up functionality working perfectly.
