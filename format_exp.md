# sc-data-format
Format for the byte arrays sent from firmware to software within the solar car and from the solar car to the telemetry visualization dashboards. The format also contains additional information about each piece of data that is collected from the solar car, as detailed below.

## Format
The following information is listed within the format for each piece of data being collected from the solar car:
* **Name** - A name to identify each piece of data.
* **Number of bytes** - The number of bytes that each piece of data takes up within the byte arrays mentioned above. This value depends on the data type (see next point).
* **Data type** - The data type that each piece of data should be interpreted as. This is used for consistency when constructing and unpacking the byte arrays.
* **Units** - The units of each piece of data, if applicable (empty string if not applicable).
* **Nominal minimum** - The nominal minimum value for each piece of data, expressed as a number and assumed to be in the same units as previously listed. The value of a given piece of data should not fall below this value during normal operation.
  * The default value, when the nominal minimum value for a piece of data is not yet known, is 0.
* **Nominal maximum** - The nominal maximum value for each piece of data, expressed as a number and assumed to be in the same units as previously listed. The value of a given piece of data should not exceed this value during normal operation.
  * The default value, when the nominal maximum value for a piece of data is not yet known, is 100.
* **Category/Subsystem** - Indicates the category or electrical subsystem that each piece of data belongs to. This value is currently only used for categorizing data when selecting what will be shown in the graphs in the engineering dashboard.
  * Ideally, each piece of data would belong to a specific electrical subsystem. However, sometimes it makes more sense to categorize certain pieces of data, like data from sensors that aren't necessarily collecting data about a specific subsystem, differently.
* **CAN ID (hex)** - a hexadecimal identifier for the CAN message associated with the datum.  This allows the firmware and software to correlate values with specific bus frames.
* **Bit offset** - the offset in bits within the CAN message payload where the value starts.  This is used when packing/unpacking the field from a CAN frame.
* **Source (optional)** - `"can"` (default) for receive/decode from CAN, or `"db"` for DB-polled transmit to CAN.
* **DB key (optional)** - row key in the DB for this signal (defaults to signal name).
* **TX mode (optional)** - currently `"on_change"` only.
* **TX minimum interval ms (optional)** - minimum interval between repeated TX updates for this signal.

For each piece of data, the information described above will be listed as follows:

&emsp;"name": [\<<i>num bytes</i>\>, "data_type", "units", \<<i>nominal min</i>\>, \<<i>nominal max</i>\>, "Category/Subsystem", "CAN ID (hex)", <i>bit offset</i>, "source?", "db_key?", "tx_mode?", <i>tx_min_interval_ms?</i>]
