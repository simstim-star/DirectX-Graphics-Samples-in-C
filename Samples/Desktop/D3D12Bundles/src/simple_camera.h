#pragma once

#include <windows.h>
#include "DirectXMathC.h"

typedef struct SimpleCamera
{
    XMFLOAT3 initialPosition;
    XMFLOAT3 position;
    float yaw;                  // Relative to the +z axis.
    float pitch;                // Relative to the xz plane.
    XMFLOAT3 lookDirection;
    XMFLOAT3 upDirection;
    float moveSpeed;            // Speed at which the camera moves, in units per second.
    float turnSpeed;            // Speed at which the camera turns, in radians per second.

    struct
    {
        bool w;
        bool a;
        bool s;
        bool d;

        bool left;
        bool right;
        bool up;
        bool down;
    } keysPressed;
} SimpleCamera;


SimpleCamera SimpleCamera_Spawn(XMFLOAT3 position);
void SimpleCamera_Update(SimpleCamera* camera, float elapsedSeconds);
void SimpleCamera_OnKeyDown(SimpleCamera* camera, WPARAM key);
void SimpleCamera_OnKeyUp(SimpleCamera* camera, WPARAM key);

XMMATRIX SimpleCamera_GetViewMatrix(XMFLOAT3 pos, XMFLOAT3 lookDirection, XMFLOAT3 upDirection);
XMMATRIX SimpleCamera_GetProjectionMatrix(float fov, float aspectRatio, float nearPlane, float farPlane);
