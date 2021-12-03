# Read data from MAX44009

        max44009@4a {
			compatible = "maxim,max44009";
			reg = <0x4a>;
			int-gpios = <&gpio0 29 0>; //Not in use
			label = "MAX44009";
		};

must be included in i2c section like

    &i2c0 {s

        compatible = "nordic,nrf-twi";
            status = "okay";
            sda-pin = <30>;
            scl-pin = <31>;

            max44009@4a {
			compatible = "maxim,max44009";
			reg = <0x4a>;
			int-gpios = <&gpio0 29 0>; //Not in use
			label = "MAX44009";
		};
    }; 