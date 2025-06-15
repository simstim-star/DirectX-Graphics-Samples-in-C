#include "simple_camera.h"
#include <math.h>
#include <stdlib.h>

/***************************************************************** 
 Private functions 
******************************************************************/

static void SimpleCamera_Reset(SimpleCamera* camera)
{
    camera->position = camera->initialPosition;
    camera->yaw = XM_PI;
    camera->pitch = 0.0f;
    camera->lookDirection = (XMFLOAT3){ 0, 0, -1 };
}

/*****************************************************************
 Public functions 
******************************************************************/

SimpleCamera SimpleCamera_Spawn(XMFLOAT3 position) {
    return (SimpleCamera){
        .initialPosition = position,
        .position = position,
        .yaw = XM_PI,
        .pitch = 0.0f,
        .lookDirection = {0, 0, -1},
        .upDirection = {0, 1, 0},
        .moveSpeed = {20.0f},
        .turnSpeed = XM_PIDIV2,
    };
}

void SimpleCamera_Update(SimpleCamera* camera, float elapsedSeconds)
{
    // Calculate the move vector in camera space.
    XMFLOAT3 move = (XMFLOAT3){ 0, 0, 0 };

    if (camera->keysPressed.a)
        move.x -= 1.0f;
    if (camera->keysPressed.d)
        move.x += 1.0f;
    if (camera->keysPressed.w)
        move.z -= 1.0f;
    if (camera->keysPressed.s)
        move.z += 1.0f;

    // Avoid normalizing zero vector (which can't be normalized)
    if (fabs(move.x) > 0.1f && fabs(move.z) > 0.1f)
    {
        XMVECTOR moveAsXMVECTOR = XMLoadFloat3(&move);
        XMVECTOR vector = XM_VEC3_NORM(moveAsXMVECTOR);
        move.x = XM_VECX(vector);
        move.z = XM_VECZ(vector);
    }

    float moveInterval = camera->moveSpeed * elapsedSeconds;
    float rotateInterval = camera->turnSpeed * elapsedSeconds;

    if (camera->keysPressed.left)
        camera->yaw += rotateInterval;
    if (camera->keysPressed.right)
        camera->yaw -= rotateInterval;
    if (camera->keysPressed.up)
        camera->pitch += rotateInterval;
    if (camera->keysPressed.down)
        camera->pitch -= rotateInterval;

    // Prevent looking too far up or down.
    camera->pitch = min(camera->pitch, XM_PIDIV4);
    camera->pitch = max(-XM_PIDIV4, camera->pitch);

    // Move the camera in model space.
    float x = move.x * -cosf(camera->yaw) - move.z * sinf(camera->yaw);
    float z = move.x * sinf(camera->yaw) - move.z * cosf(camera->yaw);
    camera->position.x += x * moveInterval;
    camera->position.z += z * moveInterval;

    // Determine the look direction.
    float r = cosf(camera->pitch);
    camera->lookDirection.x = r * sinf(camera->yaw);
    camera->lookDirection.y = sinf(camera->pitch);
    camera->lookDirection.z = r * cosf(camera->yaw);
}

XMMATRIX SimpleCamera_GetViewMatrix(XMFLOAT3 pos, XMFLOAT3 lookDirection, XMFLOAT3 upDirection)
{
	XMVECTOR EyePosition = XMLoadFloat3(&pos);
	XMVECTOR EyeDirection = XMLoadFloat3(&lookDirection);
	XMVECTOR UpDirection = XMLoadFloat3(&upDirection);
    return XM_MAT_LOOK_RH(EyePosition, EyeDirection, UpDirection);
}

XMMATRIX SimpleCamera_GetProjectionMatrix(float fov, float aspectRatio, float nearPlane, float farPlane)
{
    return XMMatrixPerspectiveFovRH(fov, aspectRatio, nearPlane, farPlane);
}

void SimpleCamera_OnKeyDown(SimpleCamera* camera, WPARAM key)
{
    switch (key)
    {
    case 'W':
        camera->keysPressed.w = true;
        break;
    case 'A':
        camera->keysPressed.a = true;
        break;
    case 'S':
        camera->keysPressed.s = true;
        break;
    case 'D':
        camera->keysPressed.d = true;
        break;
    case VK_LEFT:
        camera->keysPressed.left = true;
        break;
    case VK_RIGHT:
        camera->keysPressed.right = true;
        break;
    case VK_UP:
        camera->keysPressed.up = true;
        break;
    case VK_DOWN:
        camera->keysPressed.down = true;
        break;
    case VK_ESCAPE:
        SimpleCamera_Reset(camera);
        break;
    }
}

void SimpleCamera_OnKeyUp(SimpleCamera* camera, WPARAM key)
{
    switch (key)
    {
    case 'W':
        camera->keysPressed.w = false;
        break;
    case 'A':
        camera->keysPressed.a = false;
        break;
    case 'S':
        camera->keysPressed.s = false;
        break;
    case 'D':
        camera->keysPressed.d = false;
        break;
    case VK_LEFT:
        camera->keysPressed.left = false;
        break;
    case VK_RIGHT:
        camera->keysPressed.right = false;
        break;
    case VK_UP:
        camera->keysPressed.up = false;
        break;
    case VK_DOWN:
        camera->keysPressed.down = false;
        break;
    }
}