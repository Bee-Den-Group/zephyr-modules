# Read data from BME280

        bme280@77 {
        compatible = "bosch,bme280";
        reg = <0x77>;
        label = "BME280";
		};

must be included in i2c section like

    &i2c0 {

        compatible = "nordic,nrf-twi";
            status = "okay";
            sda-pin = <30>;
            scl-pin = <31>;

            bme280@77 {
                compatible = "bosch,bme280";
                reg = <0x77>;
                label = "BME280";
            };
    }; 