//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Dev-automation pipe (-devautomation): local named-pipe console
// command channel for test tooling. See devautomation.cpp.
//
//===========================================================================//

#ifndef DEVAUTOMATION_H
#define DEVAUTOMATION_H

#ifdef _WIN32
#pragma once
#endif

void DevAutomation_Init();
void DevAutomation_Frame();

#endif // DEVAUTOMATION_H
