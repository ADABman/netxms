/**
 * 
 */
package org.netxms.client;

/**
 * @author Victor
 *
 */
public class NXCException extends Exception
{
	private static final long serialVersionUID = -3247688667511123892L;
	private static final String[] errorTexts =
	{
      "Request completed successfully",
      "Component locked",
      "Access denied",
      "Invalid request",
      "Request timed out",
      "Request is out of state",
      "Database failure",
      "Invalid object ID",
      "Object already exist",
      "Communication failure",
      "System failure",
      "Invalid user ID",
      "Invalid argument",
      "Duplicate DCI",
      "Invalid DCI ID",
      "Out of memory",
      "Input/Output error",
      "Incompatible operation",
      "Object creation failed",
      "Loop in object relationship detected",
      "Invalid object name",
      "Invalid alarm ID",
      "Invalid action ID",
      "Operation in progress",
      "Copy operation failed for one or more DCI(s)",
      "Invalid or unknown event code",
      "No interfaces suitable for sending magic packet",
      "No MAC address on interface",
      "Command not implemented",
      "Invalid trap configuration record ID",
      "Requested data collection item is not supported by agent",
      "Client and server versions mismatch",
      "Error parsing package information file",
      "Package with specified properties already installed on server",
      "Package file already exist on server",
      "Server resource busy",
      "Invalid package ID",
      "Invalid IP address",
      "Action is used in event processing policy",
      "Variable not found",
      "Server uses incompatible version of communication protocol",
      "Address already in use",
      "Unable to select cipher",
      "Invalid public key",
      "Invalid session key",
      "Encryption is not supported by peer",
      "Server internal error",
      "Execution of external command failed",
      "Invalid object tool ID",
      "SNMP protocol error",
      "Incorrect regular expression",
      "Parameter is not supported by agent",
      "File I/O operation failed",
      "MIB file is corrupted",
      "File transfer operation already in progress",
      "Invalid job ID",
      "Invalid script ID",
      "Invalid script name",
      "Unknown map name",
      "Invalid map ID",
      "Account disabled",
      "No more grace logins",
      "Server connection broken",
      "Invalid agent configuration ID",
      "Server has lost connection with backend database",
      "Alarm is still open in helpdesk system",
      "Alarm is not in \"outstanding\" state",
      "DCI data source is not a push agent",
      "Error parsing configuration import file",
      "Configuration cannot be imported because of validation errors",
		"Invalid graph ID",
		"Local cryptographic provider failure",
		"Unsupported authentication type",
		"Bad certificate",
		"Invalid certificate ID",
		"SNMP failure",
		"Node has no support for layer 2 topology discovery",
		"Invalid situation ID",
		"Named instance not found",
		"Invalid event ID",
		"Operation cannot be completed due to agent error",
		"Unknown variable",
		"Requested resource not available",
		"Job cannot be cancelled",
		"Invalid policy ID",
		"Unknown log name",
		"Invalid log handle",
		"New password is too weak",
		"Password was used before"
	};

	private int errorCode;
	private String errorText;
	
	public NXCException(int errorCode)
	{
		super();
		this.errorCode = errorCode;
		this.errorText = null;
	}

	public NXCException(int errorCode, String errorText)
	{
		super();
		this.errorCode = errorCode;
		this.errorText = errorText;
	}

	/**
	 * @return the errorCode
	 */
	public int getErrorCode()
	{
		return errorCode;
	}

	/* (non-Javadoc)
	 * @see java.lang.Throwable#getMessage()
	 */
	@Override
	public String getMessage()
	{
		String msg = ((errorCode >= 0) && (errorCode < errorTexts.length)) ? errorTexts[errorCode] : "Error " + errorCode;
		if (errorText != null)
		{
			msg += " (" + errorText + ")";
		}
		return msg;
	}
}
