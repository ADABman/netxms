/*
** nxdbmgr - NetXMS database manager
** Copyright (C) 2004-2019 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: upgrade_v32.cpp
**
**/

#include "nxdbmgr.h"
#include <nxevent.h>

/**
 * Upgrade from 32.2 to 32.3 (also included in 31.10)
 */
static bool H_UpgradeFromV2()
{
   if (GetSchemaLevelForMajorVersion(31) < 10)
   {
      static const TCHAR *batch =
               _T("ALTER TABLE user_agent_notifications ADD on_startup char(1)\n")
               _T("UPDATE user_agent_notifications SET on_startup='0'\n")
               _T("<END>");
      CHK_EXEC(SQLBatch(batch));
      CHK_EXEC(DBSetNotNullConstraint(g_dbHandle, _T("user_agent_notifications"), _T("on_startup")));
      CHK_EXEC(SetSchemaLevelForMajorVersion(31, 10));
   }
   CHK_EXEC(SetMinorSchemaVersion(3));
   return true;
}

/**
 * Upgrade from 32.1 to 32.2
 */
static bool H_UpgradeFromV1()
{
   static const TCHAR *batch =
            _T("ALTER TABLE alarms ADD rca_script_name varchar(255)\n")
            _T("ALTER TABLE alarms ADD impact varchar(1000)\n")
            _T("ALTER TABLE event_policy ADD alarm_impact varchar(1000)\n")
            _T("<END>");
   CHK_EXEC(SQLBatch(batch));
   CHK_EXEC(SetMinorSchemaVersion(2));
   return true;
}

/**
 * Upgrade from 32.0 to 32.1
 */
static bool H_UpgradeFromV0()
{
   static const TCHAR *batch =
            _T("ALTER TABLE alarms ADD parent_alarm_id integer\n")
            _T("UPDATE alarms SET parent_alarm_id=0\n")
            _T("ALTER TABLE event_policy ADD rca_script_name varchar(255)\n")
            _T("<END>");
   CHK_EXEC(SQLBatch(batch));
   CHK_EXEC(DBSetNotNullConstraint(g_dbHandle, _T("alarms"), _T("parent_alarm_id")));

   CHK_EXEC(SetMinorSchemaVersion(1));
   return true;
}

/**
 * Upgrade map
 */
static struct
{
   int version;
   int nextMajor;
   int nextMinor;
   bool (* upgradeProc)();
} s_dbUpgradeMap[] =
{
   { 2,  32, 3, H_UpgradeFromV2 },
   { 1,  32, 2, H_UpgradeFromV1 },
   { 0,  32, 1, H_UpgradeFromV0 },
   { 0,  0,  0, NULL }
};

/**
 * Upgrade database to new version
 */
bool MajorSchemaUpgrade_V32()
{
   INT32 major, minor;
   if (!DBGetSchemaVersion(g_dbHandle, &major, &minor))
      return false;

   while((major == 32) && (minor < DB_SCHEMA_VERSION_V32_MINOR))
   {
      // Find upgrade procedure
      int i;
      for(i = 0; s_dbUpgradeMap[i].upgradeProc != NULL; i++)
         if (s_dbUpgradeMap[i].version == minor)
            break;
      if (s_dbUpgradeMap[i].upgradeProc == NULL)
      {
         _tprintf(_T("Unable to find upgrade procedure for version 32.%d\n"), minor);
         return false;
      }
      _tprintf(_T("Upgrading from version 32.%d to %d.%d\n"), minor, s_dbUpgradeMap[i].nextMajor, s_dbUpgradeMap[i].nextMinor);
      DBBegin(g_dbHandle);
      if (s_dbUpgradeMap[i].upgradeProc())
      {
         DBCommit(g_dbHandle);
         if (!DBGetSchemaVersion(g_dbHandle, &major, &minor))
            return false;
      }
      else
      {
         _tprintf(_T("Rolling back last stage due to upgrade errors...\n"));
         DBRollback(g_dbHandle);
         return false;
      }
   }
   return true;
}