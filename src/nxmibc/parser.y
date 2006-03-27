%{
/*
 * Copyright 2001, THE AUTHOR <mibparser@cvtt.net>
 * All rights reserved.
 *
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *  
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *  
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * A parser for the basic grammar to use for snmp V2c modules
 */

#pragma warning(disable : 4102)

#include <string.h>
#include <time.h>
#include "mibparse.h"
#include "nxmibc.h"
#include "nxsnmp.h"

#ifdef YYTEXT_POINTER
extern char *mptext;
#else
extern char mptext[];
#endif

#ifdef __64BIT__
#define YYSIZE_T  INT64
#endif

extern FILE *mpin, *mpout;
extern int g_nCurrLine;

static MP_MODULE *m_pModule;
static char *m_pszCurrentFilename;

int mperror(char *pszMsg);
int mplex(void);

MP_SYNTAX *create_std_syntax(int nSyntax)
{
   MP_SYNTAX *p;

   p = CREATE(MP_SYNTAX);
   p->nSyntax = nSyntax;
   return p;
}

void *mpalloc(int nSize)
{
   void *p;

   p = malloc(nSize);
   if (p != NULL)
      memset(p, 0, nSize);
   return p;
}

static int AccessFromText(char *pszText)
{
   static char *pText[] = { "read-only", "read-write", "write-only",
                            "not-accessible", "accessible-for-notify",
                            "read-create", NULL };
   char szBuffer[256];
   int i;

   for(i = 0; pText[i] != NULL; i++)
      if (strcmp(pszText, pText[i]))
         return i + 1;
   sprintf(szBuffer, "Invalid ACCESS value \"%s\"", pszText);
   mperror(szBuffer);
   return -1;
}

%}

/*
 *	Union structure.  A terminal or non-terminal can have
 *	one of these type values.
 */

%union
{
   int nInteger;
   char *pszString;
   MP_NUMERIC_VALUE number;
   DynArray *pList;
   MP_IMPORT_MODULE *pImports;
   MP_OBJECT *pObject;
   MP_SUBID *pSubId;
   MP_SYNTAX *pSyntax;
}

%token ENTERPRISE_SYM
%token TRAP_TYPE_SYM
%token VARIABLES_SYM
%token EXPLICIT_SYM
%token IMPLICIT_SYM
%token IMPLIED_SYM
%token RIGHT_BRACE_SYM
%token LEFT_BRACE_SYM
%token RIGHT_BRACKET_SYM
%token LEFT_BRACKET_SYM
%token DEFINITIONS_SYM
%token ASSIGNMENT_SYM
%token BEGIN_SYM
%token END_SYM
%token FROM_SYM
%token IMPORTS_SYM
%token EXPORTS_SYM
%token COMMA_SYM
%token SEMI_COLON_SYM
%token DOT_SYM
%token DESCRIPTION_SYM
%token ORGANIZATION_SYM
%token CONTACT_SYM
%token UPDATE_SYM
%token MODULE_IDENTITY_SYM
%token MODULE_COMPLIANCE_SYM
%token OBJECT_IDENTIFIER_SYM
%token OBJECT_TYPE_SYM
%token OBJECT_GROUP_SYM
%token OBJECT_IDENTITY_SYM
%token OBJECTS_SYM
%token MANDATORY_GROUPS_SYM
%token GROUP_SYM
%token AGENT_CAPABILITIES_SYM
%token KEYWORD_SYM
%token KEYWORD_VALUE_SYM
%token KEYWORD_BIND_SYM
%token TOKEN_SYM

%token INTEGER_SYM
%token INTEGER32_SYM
%token UNSIGNED32_SYM
%token GAUGE32_SYM
%token COUNTER_SYM
%token COUNTER32_SYM
%token COUNTER64_SYM
%token BITS_SYM
%token STRING_SYM
%token OCTET_SYM
%token SEQUENCE_SYM
%token OF_SYM
%token TIMETICKS_SYM
%token IP_ADDRESS_SYM
%token NETWORK_ADDRESS_SYM
%token OPAQUE_SYM
%token REVISION_SYM
%token TEXTUAL_CONVENTION_SYM

%token ACCESS_SYM
%token MAX_ACCESS_SYM
%token MIN_ACCESS_SYM
%token SYNTAX_SYM
%token STATUS_SYM
%token INDEX_SYM
%token REFERENCE_SYM
%token DEFVAL_SYM
%token LEFT_PAREN_SYM
%token RIGHT_PAREN_SYM
%token NOTIFICATIONS_SYM
%token NOTIFICATION_GROUP_SYM
%token NOTIFICATION_TYPE_SYM
%token SIZE_SYM
%token BAR_SYM
%token VARIATION_SYM
%token WRITE_SYNTAX_SYM
%token SUPPORTS_SYM
%token INCLUDES_SYM
%token CREATION_REQUIRES_SYM
%token PRODUCT_RELEASE_SYM
%token CHOICE_SYM
%token UNITS_SYM
%token AUGMENTS_SYM
%token OBJECT_SYM
%token TAGS_SYM
%token AUTOMATIC_SYM
%token MAX_ACCESS_SYM 
%token ACCESS_SYM
%token MIN_ACCESS_SYM

%token <pszString> MACRO_SYM
%token <pszString> MODULE_SYM
%token <pszString> UCASEFIRST_IDENT_SYM LCASEFIRST_IDENT_SYM
%token <pszString> BSTRING_SYM HSTRING_SYM CSTRING_SYM DISPLAY_HINT_SYM

%token <number> NUMBER_SYM

%type <nInteger> SnmpStatusPart SnmpAccessPart

%type <pSyntax> SnmpSyntaxPart SnmpSyntax Type BuiltInType NamedType
%type <pSyntax> TextualConventionAssignment BuiltInTypeAssignment

%type <pszString> UCidentifier LCidentifier Identifier
%type <pszString> ModuleIdentifier DefinedValue SnmpIdentityPart
%type <pszString> Symbol CharString NumericValue
%type <pszString> SnmpDescriptionPart

%type <number> Number

%type <pList> SymbolList SymbolsFromModuleList
%type <pList> AssignedIdentifierList AssignedIdentifier ObjectIdentifierList
%type <pImports> SymbolsFromModule
%type <pObject> ObjectIdentifierAssignment ObjectIdentityAssignment ObjectTypeAssignment
%type <pObject> MacroAssignment TypeOrValueAssignment ModuleIdentityAssignment
%type <pObject> SnmpObjectGroupAssignment SnmpNotificationGroupAssignment
%type <pObject> SnmpNotificationTypeAssignment AgentCapabilitiesAssignment
%type <pSubId> ObjectIdentifier

/*
 *  Type definitions of non-terminal symbols
*/

%start ModuleDefinition
%%

/*-----------------------------------------------------------------------*/
/* Module def/import/export productions */
/*-----------------------------------------------------------------------*/

ModuleDefinition:
    ModuleIdentifierAssignment AssignmentList
    End
;

AssignmentList:
    AssignmentList Assignment
|   Assignment
;

Assignment:
    ObjectIdentifierAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   ObjectIdentityAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   ObjectTypeAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   MacroAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   TypeOrValueAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   SnmpNotificationTypeAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   ModuleComplianceAssignment
|   SnmpNotificationGroupAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   SnmpObjectGroupAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   SnmpKeywordAssignment
|   AgentCapabilitiesAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   ModuleIdentityAssignment
{
   da_add(m_pModule->pObjectList, $1);
}
|   ImportsAssignment
|   ExportsAssignment
;

ModuleIdentifierAssignment:
    ModuleIdentifier
{
   m_pModule = CREATE(MP_MODULE);
   m_pModule->pszName = $1;
   m_pModule->pObjectList = da_create();
}
;

ModuleIdentifier:
    UCidentifier DEFINITIONS_SYM ASSIGNMENT_SYM Begin
{
   $$ = $1;
}
|   UCidentifier DEFINITIONS_SYM ASSIGNMENT_SYM AUTOMATIC_SYM TAGS_SYM Begin
{
   $$ = $1;
}
;

ObjectIdentifierAssignment:
    LCidentifier OBJECT_IDENTIFIER_SYM AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->pOID = $3;
}
|   LCidentifier AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->pOID = $2;
}
;

AssignedIdentifier:
    ASSIGNMENT_SYM AssignedIdentifierList
{
   $$ = $2;
}
|   ASSIGNMENT_SYM Number
{
   MP_SUBID *subid;

   subid = CREATE(MP_SUBID);
   subid->dwValue = $2.value.nInt32;
   subid->pszName = NULL;
   subid->bResolved = TRUE;
   $$ = da_create();
   da_add($$, subid);
}
;

AssignedIdentifierList:
    LEFT_BRACE_SYM ObjectIdentifierList RIGHT_BRACE_SYM
{
   $$ = $2;
}
;

ObjectIdentifierList:
    ObjectIdentifierList ObjectIdentifier
{
   da_add($$, $2);
}
|   ObjectIdentifier
{
   $$ = da_create();
   da_add($$, $1);
}
;

ObjectIdentifier:
    Number
{
   $$ = CREATE(MP_SUBID);
   $$->dwValue = $1.value.nInt32;
   $$->pszName = NULL;
   $$->bResolved = TRUE;
}
|   DefinedValue
{
   $$ = CREATE(MP_SUBID);
   $$->dwValue = 0;
   $$->pszName = $1;
   $$->bResolved = FALSE;
}
|   Identifier LEFT_PAREN_SYM Number RIGHT_PAREN_SYM
{
   $$ = CREATE(MP_SUBID);
   $$->dwValue = $3.value.nInt32;
   $$->pszName = $1;
   $$->bResolved = TRUE;
}
;

NumericValue:
    Number
{
   $$ = NULL;
}
|   DefinedValue
{
   $$ = NULL;
}
|   Number DOT_SYM DOT_SYM Number 
{
   $$ = NULL;
}
|   Identifier LEFT_PAREN_SYM Number RIGHT_PAREN_SYM
{
   $$ = NULL;
}
;

DefinedValue:
    UCidentifier DOT_SYM LCidentifier
{
   $$ = (char *)malloc(strlen($1) + strlen($3) + 2);
   sprintf($$, "%s.%s", $1, $3);
   free($1);
   free($3);
}
|   LCidentifier
{
   $$ = $1;
}
;

Number:
    NUMBER_SYM
{
   $$ = $1;
}
;

ObjectIdentityAssignment:
    LCidentifier OBJECT_IDENTITY_SYM
    SnmpStatusPart
    SnmpDescriptionPart
    SnmpReferencePart
    AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->iStatus = $3;
   $$->pszDescription = $4;
   $$->pOID = $6;
}
;

ObjectTypeAssignment:
    Identifier OBJECT_TYPE_SYM
    SnmpSyntaxPart
    SnmpUnitsPart
    SnmpAccessPart
    SnmpStatusPart
    SnmpDescriptionPart
    SnmpReferencePart
    SnmpIndexPart
    SnmpDefValPart
    AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->iSyntax = $3->nSyntax;
   $$->pszDataType = $3->pszStr;
   safe_free($3);
   $$->iAccess = $5;
   $$->iStatus = $6;
   $$->pszDescription = $7;
   $$->pOID = $11;
}
;

ModuleIdentityAssignment:
    SnmpIdentityPart
    SnmpUpdatePart
    SnmpOrganisationPart
    SnmpContactInfoPart
    SnmpDescriptionPart
    SnmpRevisionPart
    AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->iSyntax = MIB_TYPE_MODID;
   $$->pszName = $1;
   $$->pszDescription = $5;
   $$->pOID = $7;
}
;

ImportsAssignment:
    IMPORTS_SYM SymbolsFromModuleList SEMI_COLON_SYM
{
   m_pModule->pImportList = $2;
}
;

ExportsAssignment:
    EXPORTS_SYM SymbolList SEMI_COLON_SYM
;

SnmpRevisionPart:
    SnmpRevisionList
|
;

SnmpRevisionList:
    SnmpRevisionList SnmpRevisionObject
|   SnmpRevisionObject
;

SnmpRevisionObject:
    REVISION_SYM CharString
    SnmpDescriptionPart
;

SnmpIdentityPart:
    LCidentifier MODULE_IDENTITY_SYM
{
   $$ = $1;
}
;

SnmpOrganisationPart:
    ORGANIZATION_SYM CharString
;

SnmpContactInfoPart:
    CONTACT_SYM CharString
;

SnmpUpdatePart:
    UPDATE_SYM CharString
;

SnmpDescriptionPart:
    DESCRIPTION_SYM CharString
{
   $$ = $2;
}
|
{
   $$ = NULL;
}
;

ValueConstraint:
    LEFT_PAREN_SYM NumericValueConstraintList RIGHT_PAREN_SYM
|   LEFT_PAREN_SYM SIZE_SYM LEFT_PAREN_SYM NumericValueConstraintList RIGHT_PAREN_SYM RIGHT_PAREN_SYM
|   LEFT_BRACE_SYM NumericValueConstraintList RIGHT_BRACE_SYM
;

NumericValueConstraintList:
    NumericValueConstraintList BAR_SYM NumericValue 
|   NumericValueConstraintList COMMA_SYM NumericValue 
|   NumericValue 
{
}
;

SnmpKeywordAssignment:
    SnmpKeywordName
|   SnmpKeywordName SnmpKeywordValue
|   SnmpKeywordName SnmpKeywordBinding SnmpKeywordValue
;

SnmpKeywordName:
    KEYWORD_SYM ASSIGNMENT_SYM CharString
;

SnmpKeywordValue:
    KEYWORD_VALUE_SYM ASSIGNMENT_SYM CharString
;

SnmpKeywordBinding:
    KEYWORD_BIND_SYM ASSIGNMENT_SYM LEFT_BRACE_SYM SymbolList RIGHT_BRACE_SYM
;

SnmpSyntax:
    SYNTAX_SYM Type
{
   $$ = $2;
}
;

SnmpSyntaxPart:
    SnmpSyntax
{
   $$ = $1;
}
|
{
   $$ = CREATE(MP_SYNTAX);
   $$->nSyntax = MIB_TYPE_OTHER;
}
;

SnmpUnitsPart:
    UNITS_SYM CharString
|
;

SnmpWriteSyntaxPart:
    WRITE_SYNTAX_SYM Type
|
;

SnmpCreationPart:
    CREATION_REQUIRES_SYM SymbolList
|
;

TypeOrValueAssignment:
    BuiltInTypeAssignment
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_TYPEDEF;
   if ($1 != NULL)
   {
      $$->pszName = $1->pszStr;
      $$->iSyntax = $1->nSyntax;
      free($1);
   }
}
|   UCidentifier ASSIGNMENT_SYM TextualConventionAssignment
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_TEXTUAL_CONVENTION;
   $$->pszName = $1;
   if ($3 != NULL)
   {
      $$->iSyntax = $3->nSyntax;
      $$->pszDataType = $3->pszStr;
      free($3);
   }
}
|   UCidentifier ASSIGNMENT_SYM Type
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_TYPEDEF;
   $$->pszName = $1;
   if ($3 != NULL)
   {
      $$->iSyntax = $3->nSyntax;
      $$->pszDataType = $3->pszStr;
      free($3);
   }
}
|   UCidentifier ASSIGNMENT_SYM SEQUENCE_SYM SequenceAssignment
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_SEQUENCE;
   $$->pszName = $1;
}
|   UCidentifier ASSIGNMENT_SYM CHOICE_SYM SequenceAssignment
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_CHOICE;
   $$->pszName = $1;
}
|   UCidentifier ASSIGNMENT_SYM Value
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_VALUE;
   $$->pszName = $1;
}
;

Type:
    BuiltInType
{
   $$ = $1;
}
|   NamedType
{
   $$ = $1;
}
|   BuiltInType ValueConstraint
{
   $$ = $1;
}
|   NamedType ValueConstraint
{
   $$ = $1;
}
;

NamedType:
    SnmpTypeTagPart UCidentifier
{
   $$ = CREATE(MP_SYNTAX);
   $$->nSyntax = -1;
   $$->pszStr = $2;
}
;

BuiltInType:
    SnmpTypeTagPart INTEGER_SYM
{
   $$ = create_std_syntax(MIB_TYPE_INTEGER);
}
|   SnmpTypeTagPart IMPLICIT_SYM INTEGER_SYM
{
   $$ = create_std_syntax(MIB_TYPE_INTEGER);
}
|   SnmpTypeTagPart INTEGER32_SYM
{
   $$ = create_std_syntax(MIB_TYPE_INTEGER32);
}
|   SnmpTypeTagPart UNSIGNED32_SYM
{
   $$ = create_std_syntax(MIB_TYPE_UNSIGNED32);
}
|   SnmpTypeTagPart COUNTER_SYM
{
   $$ = create_std_syntax(MIB_TYPE_COUNTER);
}
|   SnmpTypeTagPart COUNTER32_SYM
{
   $$ = create_std_syntax(MIB_TYPE_COUNTER32);
}
|   SnmpTypeTagPart COUNTER64_SYM
{
   $$ = create_std_syntax(MIB_TYPE_COUNTER64);
}
|   SnmpTypeTagPart GAUGE32_SYM
{
   $$ = create_std_syntax(MIB_TYPE_GAUGE32);
}
|   SnmpTypeTagPart TIMETICKS_SYM
{
   $$ = create_std_syntax(MIB_TYPE_TIMETICKS);
}
|   SEQUENCE_SYM OF_SYM NamedType
{
   $$ = create_std_syntax(MIB_TYPE_SEQUENCE);
}
|   SnmpTypeTagPart OBJECT_IDENTIFIER_SYM
{
   $$ = create_std_syntax(MIB_TYPE_OBJID);
}
|   SnmpTypeTagPart OctetStringType
{
   $$ = create_std_syntax(MIB_TYPE_OCTETSTR);
}
;

BuiltInTypeAssignment:
    IP_ADDRESS_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_IPADDR);
   $$->pszStr = strdup("IpAddress");
}
|   OPAQUE_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_OPAQUE);
   $$->pszStr = strdup("Opaque");
}
|   INTEGER32_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_INTEGER32);
   $$->pszStr = strdup("Integer32");
}
|   UNSIGNED32_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_UNSIGNED32);
   $$->pszStr = strdup("Unsigned32");
}
|   TIMETICKS_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_TIMETICKS);
   $$->pszStr = strdup("TimeTicks");
}
|   COUNTER_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_COUNTER);
   $$->pszStr = strdup("Counter");
}
|   COUNTER32_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_COUNTER32);
   $$->pszStr = strdup("Counter32");
}
|   COUNTER64_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_COUNTER64);
   $$->pszStr = strdup("Counter64");
}
|   GAUGE32_SYM ASSIGNMENT_SYM Type
{
   $$ = create_std_syntax(MIB_TYPE_GAUGE32);
   $$->pszStr = strdup("Gauge32");
}
;

MacroAssignment:
    MACRO_SYM ASSIGNMENT_SYM Begin TokenList End
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_MACRO;
   $$->pszName = $1;
}
;

TokenList:
    TokenList TokenObject
|   TokenObject
;

TokenObject:
    TOKEN_SYM
;

TextualConventionAssignment:
    TEXTUAL_CONVENTION_SYM
    SnmpDisplayHintPart
    SnmpStatusPart
    SnmpDescriptionPart
    SnmpReferencePart
    SnmpSyntaxPart
{
   $$ = $6;
}
;

SnmpNotificationTypeAssignment:
    LCidentifier NOTIFICATION_TYPE_SYM
    SnmpObjectsPart
    SnmpStatusPart
    SnmpDescriptionPart
    SnmpReferencePart
    AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->iSyntax = MIB_TYPE_NOTIFTYPE;
   $$->iStatus = $4;
   $$->pszDescription = $5;
   $$->pOID = $7;
}
|   LCidentifier TRAP_TYPE_SYM
    ENTERPRISE_SYM LCidentifier
    SnmpTrapVariablePart
    SnmpDescriptionPart
    SnmpReferencePart
    AssignedIdentifier
{
   MP_SUBID *pSubId;

   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->iSyntax = MIB_TYPE_TRAPTYPE;
   $$->pszDescription = $6;

   pSubId = CREATE(MP_SUBID);
   pSubId->pszName = $4;
   $$->pOID = da_create();
   da_add($$->pOID, pSubId);

   pSubId = CREATE(MP_SUBID);
   pSubId->pszName = (char *)malloc(strlen($4) + 3);
   sprintf(pSubId->pszName, "%s#0", $4);
   pSubId->bResolved = TRUE;
   da_add($$->pOID, pSubId);

   da_join($$->pOID, $8);
   da_destroy($8);
}
;

SnmpTrapVariablePart:
    SnmpTrapVariableList
|
;

SnmpTrapVariableList:
    VARIABLES_SYM LEFT_BRACE_SYM SymbolList RIGHT_BRACE_SYM
;

SnmpMandatoryGroupPart:
    SnmpMandatoryGroupList
|
;

SnmpMandatoryGroupList:
    SnmpMandatoryGroupList SnmpMandatoryGroup
|   SnmpMandatoryGroup
;

SnmpMandatoryGroup:
    MANDATORY_GROUPS_SYM LEFT_BRACE_SYM SymbolList RIGHT_BRACE_SYM
;

SnmpCompliancePart:
    SnmpComplianceList
|
;

SnmpComplianceList:
    SnmpComplianceList SnmpComplianceObject
|   SnmpComplianceObject
;

SnmpComplianceObject:
    OBJECT_SYM LCidentifier
    SnmpSyntaxPart
    SnmpWriteSyntaxPart
    SnmpAccessPart
    SnmpDescriptionPart
|   GROUP_SYM LCidentifier SnmpDescriptionPart
;

SnmpObjectsPart:
    OBJECTS_SYM LEFT_BRACE_SYM SymbolList RIGHT_BRACE_SYM
|   NOTIFICATIONS_SYM LEFT_BRACE_SYM SymbolList RIGHT_BRACE_SYM
|
;

SnmpObjectGroupAssignment:
    LCidentifier OBJECT_GROUP_SYM
    SnmpObjectsPart
    SnmpStatusPart
    SnmpDescriptionPart
    AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->iStatus = $4;
   $$->iSyntax = MIB_TYPE_OBJGROUP;
   $$->pszDescription = $5;
   $$->pOID = $6;
}
;

SnmpNotificationGroupAssignment:
    LCidentifier NOTIFICATION_GROUP_SYM
    SnmpObjectsPart
    SnmpStatusPart
    SnmpDescriptionPart
    AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->iStatus = $4;
   $$->pszDescription = $5;
   $$->pOID = $6;
}
;

ModuleComplianceAssignment:
    LCidentifier MODULE_COMPLIANCE_SYM
    SnmpStatusPart
    SnmpDescriptionPart
    SnmpReferencePart
    SnmpModuleComplianceList
    AssignedIdentifier
{
   free($1);
   safe_free($4);
}
;

SnmpModuleComplianceList:
    SnmpModuleComplianceList SnmpModuleComplianceObject
|   SnmpModuleComplianceObject
;

SnmpModuleComplianceObject:
    MODULE_SYM
    SnmpMandatoryGroupPart
    SnmpCompliancePart
{
   free($1);
}
;

SnmpVariationsListPart:
    SnmpVariationsList
|
;

SnmpVariationsList:
    SnmpVariationsList SnmpVariationPart
|   SnmpVariationPart
;

SnmpVariationPart:
    VARIATION_SYM LCidentifier
    SnmpSyntaxPart
    SnmpWriteSyntaxPart
    SnmpAccessPart
    SnmpCreationPart
    SnmpDefValPart
;

ModuleCapabilitiesList:
    ModuleCapabilitiesList ModuleCapabilitiesAssignment
|   ModuleCapabilitiesAssignment
;

ModuleCapabilitiesAssignment:
    ModuleIdentifier INCLUDES_SYM SymbolList SnmpVariationsListPart
{
   safe_free($1);
}
;

AgentCapabilitiesAssignment:
    LCidentifier AGENT_CAPABILITIES_SYM
    PRODUCT_RELEASE_SYM CharString
    SnmpStatusPart
    SnmpDescriptionPart
    ModuleCapabilitiesList
    AssignedIdentifier
{
   $$ = CREATE(MP_OBJECT);
   $$->iType = MIBC_OBJECT;
   $$->pszName = $1;
   $$->iStatus = $5;
   $$->iSyntax = MIB_TYPE_AGENTCAP;
   $$->pszDescription = $6;
   $$->pOID = $8;
}
;

SnmpAccessPart:
    ACCESS_SYM LCidentifier
{
   $$ = AccessFromText($2);
}
|   MAX_ACCESS_SYM LCidentifier
{
   $$ = AccessFromText($2);
}
|   MIN_ACCESS_SYM LCidentifier
{
   $$ = AccessFromText($2);
}
|
{
   $$ = 0;
}
;

SnmpStatusPart:
    STATUS_SYM LCidentifier
{
   static char *pStatusText[] = { "mandatory", "optional", "obsolete",
                                  "deprecated", "current", NULL };
   int i;

   for(i = 0; pStatusText[i] != NULL; i++)
      if (!stricmp(pStatusText[i], $2))
      {
         $$ = i + 1;
         break;
      }
   if (pStatusText[i] == NULL)
   {
      char szBuffer[256];

      sprintf(szBuffer, "Invalid STATUS value \"%s\"", $2);
      mperror(szBuffer);
   }
}
;

SnmpReferencePart:
    REFERENCE_SYM CharString
|
;

SnmpDisplayHintPart:
    DISPLAY_HINT_SYM CharString
{
   safe_free($2);
}
|
;

SnmpIndexPart:
    INDEX_SYM LEFT_BRACE_SYM SymbolList RIGHT_BRACE_SYM
|   AUGMENTS_SYM LEFT_BRACE_SYM DefinedValue RIGHT_BRACE_SYM
|
;

SnmpDefValPart:
    DEFVAL_SYM LEFT_BRACE_SYM NumericValueItem RIGHT_BRACE_SYM
|   DEFVAL_SYM LEFT_BRACE_SYM BinaryString RIGHT_BRACE_SYM
|   DEFVAL_SYM LEFT_BRACE_SYM HexString RIGHT_BRACE_SYM
|   DEFVAL_SYM LEFT_BRACE_SYM CharString RIGHT_BRACE_SYM
|   DEFVAL_SYM AssignedIdentifierList
|
;

BinaryString:
    BSTRING_SYM
{
   safe_free($1);
}
;

HexString:
    HSTRING_SYM
{
   safe_free($1);
}
;

CharString:
    CSTRING_SYM
{
   $$ = $1;
}
;

SymbolsFromModuleList:
    SymbolsFromModuleList SymbolsFromModule
{
   da_add($$, $2);
}
|   SymbolsFromModule
{
   $$ = da_create();
   da_add($$, $1);
}
;

SymbolsFromModule:
    SymbolList FROM_SYM UCidentifier
{
   $$ = CREATE(MP_IMPORT_MODULE);
   $$->pszName = $3;
   $$->pSymbols = $1;
}
;

SymbolList:
    SymbolList COMMA_SYM Symbol
{
   da_add($$, $3);
}
|   Symbol
{
   $$ = da_create();
   da_add($$, $1);
}
;

Symbol:
    UCidentifier
{
   $$ = $1;
}
|   LCidentifier
{
   $$ = $1;
}
|   IMPLIED_SYM LCidentifier
{
   $$ = $2;
}
;

SequenceItem:
    Identifier Type
{
   safe_free($1);
   if ($2 != NULL)
   {
      safe_free($2->pszStr);
      free($2);
   }
}
;

SequenceList:
    SequenceItem
| SequenceList COMMA_SYM SequenceItem
;

SequenceAssignment:
    LEFT_BRACE_SYM SequenceList RIGHT_BRACE_SYM
;

SnmpTypeTagPart:
    SnmpTypeTagList
|
;

SnmpTypeTagList:
    SnmpTypeTagList SnmpTypeTagItem 
|   SnmpTypeTagItem
;

SnmpTypeTagItem:
    LEFT_BRACKET_SYM UCidentifier Number RIGHT_BRACKET_SYM
;

OctetStringType:
    OCTET_SYM STRING_SYM
|   EXPLICIT_SYM OCTET_SYM STRING_SYM
|   IMPLICIT_SYM OCTET_SYM STRING_SYM
|   IP_ADDRESS_SYM
|   NETWORK_ADDRESS_SYM
|   OPAQUE_SYM
|   BITS_SYM
;

Value:
    Number
{
}
|   HexString
|   BinaryString
|   CharString
{
   safe_free($1);
}
;

NumericValueItem:
    LEFT_BRACE_SYM NumericValue RIGHT_BRACE_SYM
;

Identifier:
   UCidentifier
{
   $$ = $1;
}
|  LCidentifier
{
   $$ = $1;
}
;

UCidentifier:
    UCASEFIRST_IDENT_SYM
{
   $$ = $1;
}
;

LCidentifier:
    LCASEFIRST_IDENT_SYM
{
   $$ = $1;
}
;

End:
    END_SYM
;

Begin:
    BEGIN_SYM
;

%%

MP_MODULE *ParseMIB(char *pszFilename)
{
   m_pModule = NULL;
   mpin = fopen(pszFilename, "r");
   if(mpin != NULL)
   {
	   m_pszCurrentFilename = pszFilename;
      g_nCurrLine = 1;
      InitStateStack();
      /*mpdebug=1;*/
	   mpparse();
   }
   else
   {
      Error(ERR_CANNOT_OPEN_FILE, pszFilename, strerror(errno));
      return NULL;
   }
   return m_pModule;
}

int mpwrap()
{
	return 1;
}

int mperror(char *pszMsg)
{
   Error(ERR_PARSER_ERROR, m_pszCurrentFilename, pszMsg, g_nCurrLine);
   return 0;
}
