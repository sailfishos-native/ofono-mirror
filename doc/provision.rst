Carrier Settings Provisioning Database
======================================

Carrier settings provisioning is performed automatically whenever a new,
not previously used SIM card is detected for the first time.  Settings such as
APN, username, password settings as well as the default bearer settings can be
provisioned automatically if a matching database entry is found.

Matches are performed based on the following information:
    * SIM Mobile Country Code (MCC)
    * SIM Mobile Network Code (MNC)
    * SIM Service Provider Name (SPN)

The carrier settings provisioning database is represented in JSON format and is
converted to a binary format at installation time.

JSON Structure
--------------
**(List):**
    List of mobile network provider objects.  Top level element.

    Example:
    ``[{"name": "Operator XYZ", ...}, {"name": "Operator ZYX", ...}]``

    - **name (String):**
        The name of the mobile network provider.  This field is a freeform
        string that identifies the provider.  This field is used purely for
        human consumption and not used by the provisioning logic.

        This field is `required`.

        Example: "Operator XYZ"

    - **ids (List of Strings):**
        Unique identifiers associated with the mobile network provider.  This
        is a list of all MCC+MNC identifiers associated with this provider.

        This field is `required`.

        Example: ``["99955", "99956", "99901", "99902"]``

    - **spn (String):**
        Service Provider Name associated with the mobile network provider.  This
        field is typically used to differentiate MVNOs from non-MVNO providers.

        This field is `optional`.

        Example: "ZYX"

    - **apns (List):**
        List of access points associated with the mobile network provider.  At
        least one entry must be present in the list.

        This field is `required`.

        - **name (String):**
            The descriptive name of the access point.  If present, will be
            reflected in the oFono context name after successful provisioning.

            This field is `optional`.

            Example: "Internet"

        - **apn (String):**
            Access Point Name - Setting required for successful bearer
            activation.  Provided by the carrier.

            This field is `required`.

            Example: "internet"

        - **type (List of Strings):**
            The types of connections supported by the access point.  The
            following types are recognized:

            - "internet": Used for general internet access.
            - "mms": Used for Multimedia Messaging Service (MMS).
            - "wap": Used for Wireless Application Protocol (WAP).
            - "ims": Used for IP Multimedia Subsystem (IMS).
            - "supl": Used for Secure User Plane Location (SUPL).
            - "ia": Used for Initial Attach in LTE networks.

            This field is `required`.

            Example: ``["internet", "mms"]``

        - **authentication (String):**
            Authentication method used for the connection.  The following types
            are recognized:

            - "chap": CHAP authentication
            - "pap": PAP authentication
            - "none": No authentication is used

            This field is `optional`.

            Example: "none"

        - **username (String):**
            Username used for authenticating to the access point.

            This field is `optional`.

            Example: "username"

        - **password (String):**
            Password used for authenticating to the access point.

            This field is `optional`.

            Example: "temp123"

        - **protocol (String):**
            Network protocol used for the connection.  The following types are
            recognized:

            - "ipv4": IPv4 only
            - "ipv6": IPv6 only
            - "ipv4v6": Dual protocol, both IPv4 and IPv6 will be negotiated

            If omitted, then `"ipv4v6"` will be assumed.

            This field is `optional`.

            Example: "ipv4"

        - **mmsc (String):**
            Multimedia Messaging Service Center - URL for MMS.

            This field is `required` for MMS contexts.

            Example: "foobar.mmsc:80"

        - **mmsproxy (String):**
            Proxy server for Multimedia Messaging Service (MMS).

            This field is `optional`.

            Example: "mms.proxy.net"
