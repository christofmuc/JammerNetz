import boto3
import time
import os
import uuid

# noinspection PyUnresolvedReferences
session = boto3.Session(profile_name='ecs')
ec2 = session.client('ec2')
ec2_res = session.resource('ec2')


# https://blog.ipswitch.com/how-to-create-an-ec2-instance-with-python
def create_key_pair(key_name):
    filename = key_name + '.pem'
    with open(filename, 'w') as outfile:
        key_pair = ec2_res.create_key_pair(KeyName=key_name)
        key_pair_content = str(key_pair.key_material)
        print("Created", key_pair_content)
        outfile.write(key_pair_content)
    os.chmod(filename, 0o400)


def delete_key_pair(key_name):
    filename = key_name + '.pem'
    response = ec2.delete_key_pair(KeyName=key_name)
    print(response)
    os.chmod(filename, 0o600)
    os.remove(filename)


# https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/ec2.html#EC2.Client.run_instances
def run_new_server(keyname):
    with open('cloud-init.sh', "rt") as cloud_init:
        # http://fbrnc.net/blog/2015/11/how-to-provision-an-ec2-instance
        user_data = "".join(cloud_init.readlines())

        response = ec2.run_instances(
            # ImageId='ami-0ec1ba09723e5bfac',  # Got this via console, Launch Instance wizard. This is AWS Linux 2
            ImageId='ami-0b418580298265d5c',  # This is Ubuntu 18.04 LTS
            InstanceType='m5a.large',
            AdditionalInfo='JammerNetz auto-created',
            MaxCount=1,
            MinCount=1,
            Monitoring={
                'Enabled': False
            },
            KeyName=keyname,  # Required for potential SSH access
            UserData=user_data,
            SecurityGroupIds=[
                # 'sg-69908406',
                'sg-016f395831eedd70a'
            ], )

        print(response)
    instance_id = response['Instances'][0]['InstanceId']
    print("Instance ID", instance_id)
    return instance_id


def terminate_server(instance_id):
    response = ec2.terminate_instances(
        InstanceIds=[
            instance_id,
        ],
        DryRun=False
    )
    print(response)


def get_instance_info(instance_id):
    response = ec2.describe_instances()
    # print(response)
    reservations = response['Reservations']
    for r in reservations:
        for instance in r['Instances']:
            if instance['InstanceId'] == instance_id:
                return instance
    return None


def instance_state(instance_id):
    data = get_instance_info(instance_id)
    return data['State']


def instance_status(instance_id):
    response = ec2.describe_instance_status()
    for status in response['InstanceStatuses']:
        if status['InstanceId'] == instance_id:
            return status['InstanceStatus']['Status']


def instance_public_ip(instance_id):
    data = get_instance_info(instance_id)
    return data['PublicIpAddress'], data['PublicDnsName']


# Create a keypair should we want to ssh into the machine later. Append 8 random bytes to not create conflicts with stale key pairs
key_pair_name = 'jammernetz-keys-' + str(uuid.uuid4())[:8]
create_key_pair(key_pair_name)

# Create a new server
# instanceID = run_new_server(key_pair_name)
instanceID = 'i-0d22be14c8936a40b'

# Wait for the state to become "running"
running = False
print("Waiting for instance to be in running state...", end='')
while not running:
    state = instance_state(instanceID)
    if state['Name'] == 'running':
        running = True
    else:
        print(".", end='')
        time.sleep(0.2)
print()

initialized = False
print("Waiting for instance to be done initializing...", end='')
while not initialized:
    status = instance_status(instanceID)
    if status != 'ok':
        print('.', end='')
        time.sleep(0.5)
    else:
        initialized = True
print()

# Reporting public IP here?
print(instance_public_ip(instanceID))

input("Press Enter to terminate the instance or CTRL-C to keep it...")

print("Terminating instance")
terminate_server(instanceID)

print("Deleting key pair")
delete_key_pair(key_pair_name)
