/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2010 Victor Kirhenshtein
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.base;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.InetAddress;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

public class NXCPMessage
{
	public static final int HEADER_SIZE = 16;

	// Message flags
	public static final int MF_BINARY = 0x0001;
	public static final int MF_END_OF_FILE = 0x0002;
	public static final int MF_END_OF_SEQUENCE = 0x0008;
	public static final int MF_REVERSE_ORDER = 0x0010;
	public static final int MF_CONTROL = 0x0020;
	
	private int messageCode;
	private int messageFlags;
	private long messageId;
	private Map<Long, NXCPVariable> variableMap = new HashMap<Long, NXCPVariable>(0);
	private long timestamp;
	private byte[] binaryData = null;
	private long controlData = 0;

	/**
	 * @param msgCode
	 */
	public NXCPMessage(final int msgCode)
	{
		this.messageCode = msgCode;
		messageId = 0L;
		messageFlags = 0;
	}

	/**
	 * @param msgCode
	 * @param msgId
	 */
	public NXCPMessage(final int msgCode, final long msgId)
	{
		this.messageCode = msgCode;
		this.messageId = msgId;
		messageFlags = 0;
	}

	/**
	 * Create NXCPMessage from binary NXCP message
	 *
	 * @param nxcpMessage binary NXCP message
	 * @throws java.io.IOException
	 */
	public NXCPMessage(final byte[] nxcpMessage) throws IOException
	{
		final ByteArrayInputStream byteArrayInputStream = new ByteArrayInputStream(nxcpMessage);
		//noinspection IOResourceOpenedButNotSafelyClosed
		final NXCPDataInputStream inputStream = new NXCPDataInputStream(byteArrayInputStream);

		messageCode = inputStream.readUnsignedShort();
		messageFlags = inputStream.readUnsignedShort();
		inputStream.skipBytes(4);	// Message size
		messageId = (long)inputStream.readInt();
		
		if ((messageFlags & MF_BINARY) == MF_BINARY)
		{
			final int size = inputStream.readInt();
			binaryData = new byte[size];
			inputStream.readFully(binaryData);
		}
		else if ((messageFlags & MF_CONTROL) == MF_CONTROL)
		{
			controlData = inputStream.readUnsignedInt();
		}
		else
		{
			final int numVars = inputStream.readInt();
	
			for(int i = 0; i < numVars; i++)
			{
				byteArrayInputStream.mark(byteArrayInputStream.available());
				
				// Read first 8 bytes - any DF (data field) is at least 8 bytes long
				byte[] df = new byte[16];
				inputStream.readFully(df, 0, 8);
	
				switch(df[4])
				{
					case NXCPVariable.TYPE_INT16:
						break;
					case NXCPVariable.TYPE_FLOAT:		// all these types requires additional 8 bytes
					case NXCPVariable.TYPE_INTEGER:
					case NXCPVariable.TYPE_INT64:
						inputStream.readFully(df, 8, 8);
						break;
					case NXCPVariable.TYPE_STRING:		// all these types has 4-byte length field followed by actual content
					case NXCPVariable.TYPE_BINARY:
						int size = inputStream.readInt();
						byteArrayInputStream.reset();
						df = new byte[size + 12];
						inputStream.readFully(df);
						
						// Each df aligned to 8-bytes boundary
						final int rem = (size + 12) % 8;
						if (rem != 0)
						{
							inputStream.skipBytes(8 - rem);
						}
						break;
				}
	
				final NXCPVariable variable = new NXCPVariable(df);
				variableMap.put(variable.getVariableId(), variable);
			}
		}
	}
	
	/**
	 * @return the msgCode
	 */
	public int getMessageCode()
	{
		return messageCode;
	}

	/**
	 * @param msgCode the msgCode to set
	 */
	public void setMessageCode(final int msgCode)
	{
		this.messageCode = msgCode;
	}

	/**
	 * @return the msgId
	 */
	public long getMessageId()
	{
		return messageId;
	}

	/**
	 * @param msgId the msgId to set
	 */
	public void setMessageId(final long msgId)
	{
		this.messageId = msgId;
	}

	/**
	 * @return the timestamp
	 */
	public long getTimestamp()
	{
		return timestamp;
	}

	/**
	 * @param timestamp the timestamp to set
	 */
	public void setTimestamp(final long timestamp)
	{
		this.timestamp = timestamp;
	}


	/**
	 * @param varId variable Id to find
	 */
	public NXCPVariable findVariable(final long varId)
	{
		return variableMap.get(varId);
	}

	
	//
	// Getters/Setters for variables
	//
	
	public void setVariable(final NXCPVariable variable)
	{
		variableMap.put(variable.getVariableId(), variable);
	}

	public void setVariable(final long varId, final byte[] value)
	{
		setVariable(new NXCPVariable(varId, value));
	}

	public void setVariable(final long varId, final long[] value)
	{
		setVariable(new NXCPVariable(varId, value));
	}

	public void setVariable(final long varId, final Long[] value)
	{
		setVariable(new NXCPVariable(varId, value));
	}

	public void setVariable(final long varId, final String value)
	{
		setVariable(new NXCPVariable(varId, value));
	}

	public void setVariable(final long varId, final Double value)
	{
		setVariable(new NXCPVariable(varId, value));
	}

	public void setVariable(final long varId, final InetAddress value)
	{
		setVariable(new NXCPVariable(varId, value));
	}

	public void setVariable(final long varId, final UUID value)
	{
		setVariable(new NXCPVariable(varId, value));
	}

	public void setVariableInt64(final long varId, final long value)
	{
		setVariable(new NXCPVariable(varId, NXCPVariable.TYPE_INT64, value));
	}

	public void setVariableInt32(final long varId, final int value)
	{
		setVariable(new NXCPVariable(varId, NXCPVariable.TYPE_INTEGER, (long)value));
	}

	public void setVariableInt16(final long varId, final int value)
	{
		setVariable(new NXCPVariable(varId, NXCPVariable.TYPE_INT16, (long)value));
	}
	
	public byte[] getVariableAsBinary(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsBinary() : null;
	}
	
	public String getVariableAsString(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsString() : "";
	}
	
	public Double getVariableAsReal(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsReal() : 0;
	}
	
	public int getVariableAsInteger(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsInteger().intValue() : 0;
	}
	
	public long getVariableAsInt64(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsInteger() : 0;
	}
	
	public InetAddress getVariableAsInetAddress(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsInetAddress() : null;
	}
	
	public UUID getVariableAsUUID(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsUUID() : null;
	}
	
	public long[] getVariableAsUInt32Array(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsUInt32Array() : null;
	}
	
	public Long[] getVariableAsUInt32ArrayEx(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? var.getAsUInt32ArrayEx() : null;
	}
	
	public boolean getVariableAsBoolean(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? (var.getAsInteger() != 0) : false;
	}

	public Date getVariableAsDate(final long varId)
	{
		final NXCPVariable var = findVariable(varId);
		return (var != null) ? new Date(var.getAsInteger() * 1000) : null;
	}
	

	/**
	 * Create binary NXCP message
	 * 
	 * @return byte stream ready to send
	 */
	public byte[] createNXCPMessage() throws IOException
	{
		ByteArrayOutputStream byteStream = new ByteArrayOutputStream();
		//noinspection IOResourceOpenedButNotSafelyClosed
		DataOutputStream outputStream = new DataOutputStream(byteStream);

		if ((messageFlags & MF_CONTROL) == MF_CONTROL)
		{
			outputStream.writeShort(messageCode);
			outputStream.writeShort(messageFlags);
			outputStream.writeInt(HEADER_SIZE);	   // Size
			outputStream.writeInt((int)messageId);
			outputStream.writeInt((int)controlData);
		}
		else if ((messageFlags & MF_BINARY) == MF_BINARY) {
			outputStream.writeShort(messageCode); // wCode
			outputStream.writeShort(messageFlags); // wFlags
			final int packetSize = binaryData.length + HEADER_SIZE + (8 - ((binaryData.length + HEADER_SIZE) % 8)) & 7;
			outputStream.writeInt(packetSize); // dwSize (padded to 8 bytes boundaries)
			outputStream.writeInt((int)messageId); // dwId
			outputStream.writeInt(binaryData.length); // dwNumVars, here used for real size of the payload (w/o headers and padding)
			outputStream.write(binaryData);
		}
		else
		{
			// Create byte array with all variables
			for(final NXCPVariable nxcpVariable: variableMap.values())
			{
				final byte[] field = nxcpVariable.createNXCPDataField();
				outputStream.write(field);
			}
			final byte[] payload = byteStream.toByteArray();

			// Create message header in new byte stream and add payload
			byteStream = new ByteArrayOutputStream();
			//noinspection IOResourceOpenedButNotSafelyClosed
			outputStream = new DataOutputStream(byteStream);
			outputStream.writeShort(messageCode);
			outputStream.writeShort(messageFlags);
			outputStream.writeInt(payload.length + HEADER_SIZE);	   // Size
			outputStream.writeInt((int)messageId);
			outputStream.writeInt(variableMap.size());
			outputStream.write(payload);
		}

		return byteStream.toByteArray();
	}

	/**
	 * Get data of raw message. Will return null if message is not a raw message.
	 * 
	 * @return Binary data of raw message
	 */
	public byte[] getBinaryData()
	{
		return binaryData;
	}

	/**
	 * Set data for raw message.
	 * 
	 * @param binaryData
	 */
	public void setBinaryData(final byte[] binaryData)
	{
		this.binaryData = binaryData;
	}
	
	/**
	 * Return true if message is a raw message
	 * @return raw message flag
	 */
	public boolean isBinaryMessage()
	{
		return (messageFlags & MF_BINARY) == MF_BINARY;
	}

	/**
	 * Set or clear raw (binary) message flag
	 * 
	 * @param isControl
	 *           true to set control message flag
	 */
	public void setBinaryMessage(boolean isRaw)
	{
		if (isRaw)
		{
			messageFlags |= MF_BINARY;
		}
		else
		{
			messageFlags &= ~MF_BINARY;
		}
	}
	
	/**
	 * Return true if message is a control message
	 * @return control message flag
	 */
	public boolean isControlMessage()
	{
		return (messageFlags & MF_CONTROL) == MF_CONTROL;
	}
	
	/**
	 * Set or clear control message flag
	 * 
	 * @param isControl true to set control message flag
	 */
	public void setControl(boolean isControl)
	{
		if (isControl)
			messageFlags |= MF_CONTROL;
		else
			messageFlags &= ~MF_CONTROL;
	}
	
	/**
	 * Return true if message has "end of file" flag set
	 * @return "end of file" flag
	 */
	public boolean isEndOfFile()
	{
		return (messageFlags & MF_END_OF_FILE) == MF_END_OF_FILE;
	}
	
	/**
	 * Set end of file message flag
	 * 
	 * @param isEOF true to set end of file message flag
	 */
	public void setEndOfFile(boolean isEOF)
	{
		if (isEOF)
			messageFlags |= MF_END_OF_FILE;
		else
			messageFlags &= ~MF_END_OF_FILE;
	}
	
	/**
	 * Return true if message has "end of sequence" flag set
	 * @return "end of file" flag
	 */
	public boolean isEndOfSequence()
	{
		return (messageFlags & MF_END_OF_SEQUENCE) == MF_END_OF_SEQUENCE;
	}

	/**
	 * Set end of sequence message flag
	 * 
	 * @param isEOS true to set end of sequence message flag
	 */
	public void setEndOfSequence(boolean isEOS)
	{
		if (isEOS)
			messageFlags |= MF_END_OF_SEQUENCE;
		else
			messageFlags &= ~MF_END_OF_SEQUENCE;
	}

	/**
	 * @return the controlData
	 */
	public long getControlData()
	{
		return controlData;
	}

	/**
	 * @param controlData the controlData to set
	 */
	public void setControlData(long controlData)
	{
		this.controlData = controlData;
	}
}
